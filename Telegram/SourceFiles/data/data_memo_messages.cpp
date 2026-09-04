/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_memo_messages.h"

#include "base/unixtime.h"
#include "core/file_location.h"
#include "core/mime_type.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item_helpers.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "ui/chat/attach/attach_prepare.h"
#include "memo/memo_archive.h"
#include "ui/image/image_location_factory.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

#include <cstring>

namespace Data {
namespace {

[[nodiscard]] MessageFlags MemoItemFlags() {
	return MessageFlag::FakeHistoryItem
		| MessageFlag::Local
		| MessageFlag::Outgoing
		| MessageFlag::HasFromId
		| MessageFlag::HideEdited;
}

[[nodiscard]] MemoMedia::Type DetectMediaType(
		const FilePrepareResult &result) {
	if (result.type == SendMediaType::Photo) {
		return MemoMedia::Type::Photo;
	} else if (result.type == SendMediaType::Audio) {
		return MemoMedia::Type::Voice;
	}
	const auto mime = result.filemime;
	if (mime.startsWith(u"video/"_q)) {
		return MemoMedia::Type::Video;
	} else if (mime.startsWith(u"audio/"_q)) {
		return MemoMedia::Type::Audio;
	}
	return MemoMedia::Type::File;
}

[[nodiscard]] SendMediaType SendTypeForMedia(MemoMedia::Type type) {
	return (type == MemoMedia::Type::Photo)
		? SendMediaType::Photo
		: (type == MemoMedia::Type::Voice)
		? SendMediaType::Audio
		: SendMediaType::File;
}

[[nodiscard]] MTPMessageMedia PrepareMediaData(
		const FilePrepareResult &result,
		bool spoiler) {
	if (result.type == SendMediaType::Photo) {
		using Flag = MTPDmessageMediaPhoto::Flag;
		return MTP_messageMediaPhoto(
			MTP_flags(Flag::f_photo | (spoiler ? Flag::f_spoiler : Flag())),
			result.photo,
			MTPint(),
			MTPDocument());
	}
	using Flag = MTPDmessageMediaDocument::Flag;
	const auto voice = (result.type == SendMediaType::Audio);
	return MTP_messageMediaDocument(
		MTP_flags(Flag::f_document
			| (voice ? Flag::f_voice : Flag())
			| (spoiler ? Flag::f_spoiler : Flag())),
		result.document,
		MTPVector<MTPDocument>(),
		MTPPhoto(),
		MTPint(),
		MTPint());
}

[[nodiscard]] QSize ResultDimensions(const FilePrepareResult &result) {
	const auto i = result.photoThumbs.find('y');
	return (i != end(result.photoThumbs))
		? i->second.image.size()
		: QSize();
}

[[nodiscard]] QString MemoMessagePreview(const MemoMessage &message) {
	if (!message.text.text.isEmpty()) {
		return message.text.text;
	} else if (!message.media) {
		return QString();
	}
	switch (message.media->type) {
	case MemoMedia::Type::Photo: return tr::lng_in_dlg_photo(tr::now);
	case MemoMedia::Type::Voice: return tr::lng_in_dlg_audio(tr::now);
	case MemoMedia::Type::Audio: return tr::lng_in_dlg_audio_file(tr::now);
	case MemoMedia::Type::Video: return tr::lng_in_dlg_video(tr::now);
	case MemoMedia::Type::File: break;
	}
	return message.media->originalName.isEmpty()
		? tr::lng_in_dlg_file(tr::now)
		: message.media->originalName;
}

[[nodiscard]] QByteArray PackWaveform(const VoiceWaveform &waveform) {
	return QByteArray(
		reinterpret_cast<const char*>(waveform.constData()),
		waveform.size());
}

[[nodiscard]] VoiceWaveform UnpackWaveform(const QByteArray &packed) {
	auto result = VoiceWaveform();
	result.resize(packed.size());
	memcpy(result.data(), packed.constData(), packed.size());
	return result;
}

} // namespace

MemoMessages::MemoMessages(not_null<Session*> owner)
: _owner(owner)
, _session(&owner->session())
, _history(owner->history(owner->session().userPeerId()))
, _folders(ReadMemoFolders(&owner->session())) {
	_owner->itemRemoved(
	) | rpl::filter([](not_null<const HistoryItem*> item) {
		return item->isLocalMemo();
	}) | rpl::on_next([=](not_null<const HistoryItem*> item) {
		remove(item);
	}, _lifetime);
}

MemoMessages::~MemoMessages() = default;

not_null<History*> MemoMessages::history() const {
	return _history;
}

const std::vector<MemoFolder> &MemoMessages::folders() const {
	return _folders;
}

rpl::producer<> MemoMessages::foldersChanged() const {
	return _foldersChanged.events();
}

const MemoFolder *MemoMessages::folder(uint64 folderId) const {
	const auto i = ranges::find(_folders, folderId, &MemoFolder::id);
	return (i != end(_folders)) ? &*i : nullptr;
}

uint64 MemoMessages::defaultFolder() {
	if (_folders.empty()) {
		return createFolder(tr::lng_memo_folder_default(tr::now));
	}
	return _folders.front().id;
}

uint64 MemoMessages::createFolder(const QString &title) {
	const auto now = base::unixtime::now();
	const auto id = GenerateMemoId();
	_folders.push_back({
		.id = id,
		.title = title,
		.order = int(_folders.size()),
		.created = now,
		.updated = now,
		.revision = 1,
	});
	saveFolders();
	return id;
}

void MemoMessages::renameFolder(uint64 folderId, const QString &title) {
	const auto i = ranges::find(_folders, folderId, &MemoFolder::id);
	if (i == end(_folders) || i->title == title) {
		return;
	}
	i->title = title;
	i->updated = base::unixtime::now();
	++i->revision;
	saveFolders();
}

void MemoMessages::removeFolder(uint64 folderId) {
	const auto i = ranges::find(_folders, folderId, &MemoFolder::id);
	if (i == end(_folders)) {
		return;
	}
	_folders.erase(i);
	auto order = 0;
	for (auto &folder : _folders) {
		folder.order = order++;
	}

	// Ownership is dropped before destroying, so that the removal handler
	// does not have to find the item and the loop always moves forward.
	_removingFolder = folderId;
	while (true) {
		const auto list = _data.find(folderId);
		if (list == end(_data) || list->second.items.empty()) {
			break;
		}
		const auto j = list->second.items.begin();
		const auto item = j->second.release();
		list->second.items.erase(j);
		if (item) {
			item->destroy();
		}
	}
	_removingFolder = 0;
	_data.remove(folderId);

	RemoveMemoFolderData(_session, folderId);
	saveFolders();
	notify(folderId);
}

void MemoMessages::applyFolderOrder(const std::vector<uint64> &order) {
	auto sorted = std::vector<MemoFolder>();
	sorted.reserve(_folders.size());
	for (const auto id : order) {
		const auto i = ranges::find(_folders, id, &MemoFolder::id);
		if (i != end(_folders)) {
			sorted.push_back(*i);
		}
	}
	for (const auto &folder : _folders) {
		if (!ranges::contains(order, folder.id)) {
			sorted.push_back(folder);
		}
	}
	auto index = 0;
	for (auto &folder : sorted) {
		folder.order = index++;
	}
	_folders = std::move(sorted);
	saveFolders();
}

void MemoMessages::ensureCommandsLoaded() {
	for (const auto &folder : _folders) {
		ensureLoaded(folder.id);
	}
}

std::vector<MemoCommand> MemoMessages::commands(const QString &filter) {
	ensureCommandsLoaded();

	auto result = std::vector<MemoCommand>();
	for (const auto &folder : _folders) {
		const auto i = _data.find(folder.id);
		if (i == end(_data)) {
			continue;
		}
		for (const auto &message : i->second.manifest.messages) {
			if (message.command.isEmpty()
				|| !message.command.startsWith(filter, Qt::CaseInsensitive)) {
				continue;
			}
			const auto j = ranges::find(
				result,
				message.command,
				&MemoCommand::command);
			if (j != end(result)) {
				++j->count;
				j->preview = QString();
				j->folderId = 0;
				j->messageId = 0;
				continue;
			}
			result.push_back({
				.command = message.command,
				.count = 1,
				.preview = MemoMessagePreview(message),
				.folderId = folder.id,
				.messageId = message.id,
			});
		}
	}
	ranges::sort(result, ranges::less(), &MemoCommand::command);
	return result;
}

std::vector<MemoCommandMessage> MemoMessages::commandMessages(
		const QString &command) {
	ensureCommandsLoaded();

	auto result = std::vector<MemoCommandMessage>();
	if (command.isEmpty()) {
		return result;
	}
	for (const auto &folder : _folders) {
		const auto i = _data.find(folder.id);
		if (i == end(_data)) {
			continue;
		}
		for (const auto &message : i->second.manifest.messages) {
			if (message.command.compare(command, Qt::CaseInsensitive) != 0) {
				continue;
			}
			result.push_back({
				.folderId = folder.id,
				.messageId = message.id,
				.preview = MemoMessagePreview(message),
			});
		}
	}
	return result;
}

HistoryItem *MemoMessages::commandItem(uint64 folderId, uint64 messageId) {
	ensureLoaded(folderId);

	const auto i = _data.find(folderId);
	if (i == end(_data)) {
		return nullptr;
	}
	const auto j = ranges::find(
		i->second.manifest.messages,
		messageId,
		&MemoMessage::id);
	return (j != end(i->second.manifest.messages))
		? materialize(folderId, *j)
		: nullptr;
}

void MemoMessages::setCommand(
		not_null<const HistoryItem*> item,
		QString command) {
	const auto folderId = lookupFolder(item);
	const auto messageIt = _itemToMessage.find(item);
	if (!folderId || messageIt == end(_itemToMessage)) {
		return;
	}
	const auto i = _data.find(folderId);
	if (i == end(_data)) {
		return;
	}
	const auto j = ranges::find(
		i->second.manifest.messages,
		messageIt->second,
		&MemoMessage::id);
	if (j == end(i->second.manifest.messages) || j->command == command) {
		return;
	}
	j->command = std::move(command);
	++j->revision;
	save(folderId);
}

QString MemoMessages::itemCommand(not_null<const HistoryItem*> item) const {
	const auto message = lookupMessage(item);
	return message ? message->command : QString();
}

std::vector<Memo::ArchiveFolder> MemoMessages::collectForArchive(
		const std::vector<uint64> &folderIds) {
	auto result = std::vector<Memo::ArchiveFolder>();
	auto used = base::flat_set<QString>();
	for (const auto folderId : folderIds) {
		const auto folder = this->folder(folderId);
		if (!folder) {
			continue;
		}
		ensureLoaded(folderId);
		const auto i = _data.find(folderId);
		if (i == end(_data)) {
			continue;
		}
		const auto title = UniqueArchiveTitle(folder->title, used);
		used.emplace(title);

		auto entry = Memo::ArchiveFolder{
			.title = title,
			.folder = SerializeMemoFolder(*folder),
			.manifest = SerializeMemoManifest(folderId, i->second.manifest),
		};
		auto names = base::flat_set<QString>();
		for (const auto &message : i->second.manifest.messages) {
			if (!message.media || names.contains(message.media->file)) {
				continue;
			}
			const auto path = MemoFilePath(
				_session,
				folderId,
				message.media->file);
			if (path.isEmpty() || !QFileInfo::exists(path)) {
				continue;
			}
			names.emplace(message.media->file);
			entry.files.push_back({ message.media->file, path });
		}
		result.push_back(std::move(entry));
	}
	return result;
}

uint64 MemoMessages::importFolder(const Memo::UnpackedFolder &unpacked) {
	auto manifest = ParseMemoManifest(unpacked.manifest);
	if (manifest.messages.empty()) {
		return 0;
	}
	const auto title = ParseMemoFolderTitle(unpacked.folder, unpacked.title);
	const auto folderId = createFolder(title);
	auto &list = _data[folderId];
	list.loaded = true;

	if (!unpacked.directory.isEmpty()) {
		const auto from = QDir(unpacked.directory);
		for (const auto &name : from.entryList(QDir::Files)) {
			const auto target = MemoFilePath(_session, folderId, name);
			if (!target.isEmpty()
				&& QDir().mkpath(QFileInfo(target).absolutePath())) {
				QFile::rename(from.absoluteFilePath(name), target);
			}
		}
	}
	for (auto &message : manifest.messages) {
		if (message.media
			&& !QFileInfo::exists(
				MemoFilePath(_session, folderId, message.media->file))) {
			message.media = std::nullopt;
		}
	}
	list.manifest = std::move(manifest);
	save(folderId);
	notify(folderId);
	return folderId;
}

void MemoMessages::saveFolders() {
	WriteMemoFolders(_session, _folders);
	_foldersChanged.fire({});
}

rpl::producer<> MemoMessages::updates(uint64 folderId) {
	ensureLoaded(folderId);

	return _updates.events(
	) | rpl::filter([=](uint64 value) {
		return (value == folderId);
	}) | rpl::to_empty;
}

MessagesSlice MemoMessages::list(
		uint64 folderId,
		MessagePosition aroundId,
		int limitBefore,
		int limitAfter) {
	ensureLoaded(folderId);

	auto result = MessagesSlice();
	result.skippedBefore = result.skippedAfter = 0;
	const auto i = _data.find(folderId);
	if (i == end(_data)) {
		result.fullCount = 0;
		return result;
	}
	auto &list = i->second;
	const auto &messages = list.manifest.messages;
	result.fullCount = int(messages.size());
	if (messages.empty()) {
		return result;
	}

	// Messages are sorted by their memo id, which grows with time, so the
	// oldest one is at the front. The list widget counts "before" towards
	// the older side, the same way the replies list does.
	const auto around = aroundId.fullId.msg;
	const auto position = [&] {
		if (!around) {
			return int(messages.size());
		}
		const auto item = _owner->message(_history->peer->id, around);
		const auto messageIt = item
			? _itemToMessage.find(item)
			: end(_itemToMessage);
		if (messageIt == end(_itemToMessage)) {
			return int(messages.size());
		}
		const auto j = ranges::lower_bound(
			messages,
			messageIt->second,
			ranges::less(),
			&MemoMessage::id);
		return int(j - begin(messages));
	}();

	const auto availableBefore = position;
	const auto availableAfter = int(messages.size()) - position;
	const auto useBefore = std::min(availableBefore, limitBefore + 1);
	const auto useAfter = std::min(availableAfter, limitAfter);
	result.skippedBefore = availableBefore - useBefore;
	result.skippedAfter = availableAfter - useAfter;

	const auto from = begin(messages) + (position - useBefore);
	const auto till = begin(messages) + (position + useAfter);
	result.ids.reserve(useBefore + useAfter);
	auto nearest = FullMsgId();
	for (auto j = from; j != till; ++j) {
		const auto item = materialize(folderId, *j);
		if (!item) {
			continue;
		}
		result.ids.push_back(item->fullId());
		if (!nearest || (j - from) < useBefore) {
			nearest = item->fullId();
		}
	}
	result.nearestToAround = nearest;
	return result;
}

void MemoMessages::ensureLoaded(uint64 folderId) {
	// A view may still ask for a folder that was just removed, loading it
	// again would bring a dead entry back into the map.
	if (!folderId || !folder(folderId)) {
		return;
	}
	auto &list = _data[folderId];
	if (list.loaded) {
		return;
	}
	list.loaded = true;
	list.manifest = ReadMemoManifest(_session, folderId);
}

HistoryItem *MemoMessages::materialize(
		uint64 folderId,
		const MemoMessage &message) {
	auto &list = _data[folderId];
	const auto already = list.items.find(message.id);
	if (already != end(list.items)) {
		return already->second.get();
	} else if (!message.media) {
		return createItem(folderId, message, MTP_messageMediaEmpty());
	}
	const auto &media = *message.media;
	const auto path = MemoFilePath(_session, folderId, media.file);
	if (!QFileInfo::exists(path)) {
		return nullptr;
	}
	const auto to = FileLoadTo(_history->peer->id, {}, FullReplyTo(), MsgId());
	auto task = std::optional<FileLoadTask>();
	if (media.type == MemoMedia::Type::Voice) {
		auto file = QFile(path);
		auto content = file.open(QIODevice::ReadOnly)
			? file.readAll()
			: QByteArray();
		if (content.isEmpty()) {
			return nullptr;
		}
		task.emplace(FileLoadTask::VoiceArgs{
			.session = _session,
			.voice = std::move(content),
			.duration = media.duration,
			.waveform = UnpackWaveform(media.waveform),
			.to = to,
		});
	} else {
		task.emplace(FileLoadTask::Args{
			.session = _session,
			.filepath = path,
			.type = SendTypeForMedia(media.type),
			.to = to,
			.spoiler = media.spoiler,
			.displayName = media.originalName,
		});
	}
	task->process({ .generateGoodThumbnail = false });
	const auto &result = task->peekResult();
	if (!result || result->filesize <= 0) {
		return nullptr;
	}
	registerMedia(*result, path);
	return createItem(
		folderId,
		message,
		PrepareMediaData(*result, media.spoiler));
}

QString MemoMessages::saveResultFile(
		uint64 folderId,
		const FilePrepareResult &result) {
	const auto extension = QFileInfo(result.filename).suffix();
	if (!result.fileparts.empty()) {
		auto bytes = QByteArray();
		bytes.reserve(result.partssize);
		for (const auto &part : result.fileparts) {
			bytes.append(part);
		}
		return SaveMemoFileContent(_session, folderId, bytes, extension);
	} else if (!result.content.isEmpty()) {
		return SaveMemoFileContent(
			_session,
			folderId,
			result.content,
			extension);
	} else if (!result.filepath.isEmpty()) {
		return CopyMemoFile(_session, folderId, result.filepath);
	}
	return QString();
}

void MemoMessages::registerMedia(
		const FilePrepareResult &result,
		const QString &path) {
	if (result.type == SendMediaType::Photo) {
		_owner->processPhoto(result.photo, result.photoThumbs);
		return;
	}
	const auto document = result.thumb.isNull()
		? _owner->processDocument(result.document)
		: _owner->processDocument(
			result.document,
			Images::FromImageInMemory(
				result.thumb,
				"JPG",
				result.thumbbytes));
	if (const auto active = document->activeMediaView()) {
		if (!result.thumb.isNull()) {
			active->setThumbnail(result.thumb);
		}
	}
	document->setLocation(Core::FileLocation(path));
}

MemoMessage *MemoMessages::appendMessage(
		uint64 folderId,
		const QString &fileName,
		const QString &originalName,
		const FilePrepareResult &result,
		TextWithTags caption,
		uint64 groupId,
		bool spoiler,
		crl::time duration,
		const VoiceWaveform &waveform) {
	auto &list = _data[folderId];
	auto message = MemoMessage{
		.id = list.manifest.nextMessageId++,
		.date = base::unixtime::now(),
		.text = std::move(caption),
		.groupId = groupId,
		.revision = 1,
		.media = MemoMedia{
			.type = DetectMediaType(result),
			.file = fileName,
			.originalName = originalName,
			.mime = result.filemime,
			.size = result.filesize,
			.dimensions = ResultDimensions(result),
			.duration = duration,
			.waveform = PackWaveform(waveform),
			.spoiler = spoiler,
		},
	};
	list.manifest.messages.push_back(std::move(message));
	return &list.manifest.messages.back();
}

not_null<HistoryItem*> MemoMessages::createItem(
		uint64 folderId,
		const MemoMessage &message,
		const MTPMessageMedia &media) {
	auto fields = HistoryItemCommonFields{
		.id = nextItemId(),
		.flags = MemoItemFlags(),
		.from = _session->userPeerId(),
		.date = message.date,
		.groupedId = message.groupId,
	};
	const auto item = _history->addNewLocalMessage(
		std::move(fields),
		TextWithEntities{
			message.text.text,
			TextUtilities::ConvertTextTagsToEntities(message.text.tags),
		},
		media);
	auto &list = _data[folderId];
	list.items.emplace(message.id, OwnedItem(item));
	_itemToFolder.emplace(item, folderId);
	_itemToMessage.emplace(item, message.id);
	return item;
}

MsgId MemoMessages::nextItemId() {
	if (_lastItemId + 1 >= MemoMaxMsgId) {
		_lastItemId = MemoMinMsgId;
	}
	return ++_lastItemId;
}

void MemoMessages::sendText(uint64 folderId, TextWithTags text) {
	if (!folderId || text.text.isEmpty()) {
		return;
	}
	ensureLoaded(folderId);

	auto &list = _data[folderId];
	auto message = MemoMessage{
		.id = list.manifest.nextMessageId++,
		.date = base::unixtime::now(),
		.text = std::move(text),
		.revision = 1,
	};
	createItem(folderId, message, MTP_messageMediaEmpty());
	list.manifest.messages.push_back(std::move(message));
	save(folderId);
	notify(folderId);
}

void MemoMessages::sendFiles(
		uint64 folderId,
		const std::shared_ptr<Ui::PreparedBundle> &bundle) {
	if (!folderId || !bundle) {
		return;
	}
	ensureLoaded(folderId);

	const auto compress = bundle->way.sendImagesAsPhotos();
	const auto to = FileLoadTo(_history->peer->id, {}, FullReplyTo(), MsgId());
	for (auto &group : bundle->groups) {
		const auto grouped = (group.type != Ui::AlbumType::None)
			&& (group.list.files.size() > 1);
		const auto groupId = grouped ? GenerateMemoId() : uint64(0);
		for (auto &file : group.list.files) {
			const auto photo = compress
				&& (file.type == Ui::PreparedFile::Type::Photo);
			auto task = FileLoadTask(FileLoadTask::Args{
				.session = _session,
				.filepath = file.path,
				.content = file.content,
				.information = std::move(file.information),
				.type = photo ? SendMediaType::Photo : SendMediaType::File,
				.to = to,
				.caption = file.caption,
				.spoiler = file.spoiler,
				.forceFile = !photo,
				.animationJob = file.animationJob,
				.displayName = file.displayName,
			});
			task.process({ .generateGoodThumbnail = false });
			const auto &result = task.peekResult();
			if (!result || result->filesize <= 0) {
				continue;
			}
			const auto name = saveResultFile(folderId, *result);
			if (name.isEmpty()) {
				continue;
			}
			const auto path = MemoFilePath(_session, folderId, name);
			const auto originalName = !file.displayName.isEmpty()
				? file.displayName
				: !result->filename.isEmpty()
				? result->filename
				: QFileInfo(file.path).fileName();
			const auto message = appendMessage(
				folderId,
				name,
				originalName,
				*result,
				file.caption,
				groupId,
				file.spoiler,
				0,
				VoiceWaveform());
			registerMedia(*result, path);
			createItem(
				folderId,
				*message,
				PrepareMediaData(*result, file.spoiler));
		}
	}
	save(folderId);
	notify(folderId);
}

void MemoMessages::sendVoice(
		uint64 folderId,
		QByteArray bytes,
		VoiceWaveform waveform,
		crl::time duration) {
	if (!folderId || bytes.isEmpty()) {
		return;
	}
	ensureLoaded(folderId);

	const auto name = SaveMemoFileContent(
		_session,
		folderId,
		bytes,
		u"ogg"_q);
	if (name.isEmpty()) {
		return;
	}
	const auto path = MemoFilePath(_session, folderId, name);
	const auto to = FileLoadTo(_history->peer->id, {}, FullReplyTo(), MsgId());
	auto task = FileLoadTask(FileLoadTask::VoiceArgs{
		.session = _session,
		.voice = std::move(bytes),
		.duration = duration,
		.waveform = waveform,
		.to = to,
	});
	task.process({ .generateGoodThumbnail = false });
	const auto &result = task.peekResult();
	if (!result || result->filesize <= 0) {
		RemoveMemoFile(_session, folderId, name);
		return;
	}
	const auto message = appendMessage(
		folderId,
		name,
		QString(),
		*result,
		TextWithTags(),
		0,
		false,
		duration,
		waveform);
	registerMedia(*result, path);
	createItem(folderId, *message, PrepareMediaData(*result, false));

	save(folderId);
	notify(folderId);
}

void MemoMessages::editText(
		not_null<HistoryItem*> item,
		TextWithTags text) {
	const auto folderId = lookupFolder(item);
	if (!folderId) {
		return;
	}
	const auto messageIt = _itemToMessage.find(item);
	const auto listIt = _data.find(folderId);
	if (messageIt == end(_itemToMessage) || listIt == end(_data)) {
		return;
	}
	auto &list = listIt->second;
	const auto i = ranges::find(
		list.manifest.messages,
		messageIt->second,
		&MemoMessage::id);
	if (i == end(list.manifest.messages)) {
		return;
	}
	i->text = text;
	i->editDate = base::unixtime::now();
	++i->revision;
	item->setText(TextWithEntities{
		text.text,
		TextUtilities::ConvertTextTagsToEntities(text.tags),
	});
	save(folderId);
	notify(folderId);
}

uint64 MemoMessages::lookupFolder(not_null<const HistoryItem*> item) const {
	const auto i = _itemToFolder.find(item);
	return (i != end(_itemToFolder)) ? i->second : 0;
}

const MemoMessage *MemoMessages::lookupMessage(
		not_null<const HistoryItem*> item) const {
	const auto folderId = lookupFolder(item);
	if (!folderId) {
		return nullptr;
	}
	const auto messageIt = _itemToMessage.find(item);
	if (messageIt == end(_itemToMessage)) {
		return nullptr;
	}
	const auto list = _data.find(folderId);
	if (list == end(_data)) {
		return nullptr;
	}
	const auto i = ranges::find(
		list->second.manifest.messages,
		messageIt->second,
		&MemoMessage::id);
	return (i != end(list->second.manifest.messages)) ? &*i : nullptr;
}

void MemoMessages::deleteMessage(not_null<const HistoryItem*> item) {
	const auto folderId = lookupFolder(item);
	const auto messageIt = _itemToMessage.find(item);
	if (!folderId || messageIt == end(_itemToMessage)) {
		return;
	}
	const auto i = _data.find(folderId);
	if (i == end(_data)) {
		return;
	}
	auto &list = i->second;
	const auto j = ranges::find(
		list.manifest.messages,
		messageIt->second,
		&MemoMessage::id);
	if (j == end(list.manifest.messages)) {
		return;
	}
	const auto file = j->media ? j->media->file : QString();
	list.manifest.messages.erase(j);
	if (!file.isEmpty()) {
		const auto stillUsed = ranges::any_of(
			list.manifest.messages,
			[&](const MemoMessage &message) {
				return message.media && (message.media->file == file);
			});
		if (!stillUsed) {
			RemoveMemoFile(_session, folderId, file);
		}
	}
	save(folderId);
}

void MemoMessages::remove(not_null<const HistoryItem*> item) {
	const auto folderId = lookupFolder(item);
	if (!folderId) {
		return;
	}
	const auto messageIt = _itemToMessage.find(item);
	const auto messageId = (messageIt != end(_itemToMessage))
		? messageIt->second
		: uint64(0);
	_itemToFolder.remove(item);
	_itemToMessage.remove(item);

	const auto i = _data.find(folderId);
	if (i == end(_data)) {
		return;
	}
	auto &list = i->second;
	const auto k = list.items.find(messageId);
	if (k != end(list.items) && k->second.get() == item.get()) {
		k->second.release();
		list.items.erase(k);
	}
	if (_removingFolder != folderId) {
		notify(folderId);
	}
}

void MemoMessages::save(uint64 folderId) {
	const auto i = _data.find(folderId);
	if (i != end(_data)) {
		WriteMemoManifest(_session, folderId, i->second.manifest);
	}
}

void MemoMessages::notify(uint64 folderId) {
	_updates.fire_copy(folderId);
}

} // namespace Data
