/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_memo_storage.h"

#include "base/random.h"
#include "base/openssl_help.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Data {
namespace {

constexpr auto kFormatVersion = 1;
constexpr auto kMaxFolderTitle = 64;
constexpr auto kMaxCommandLength = 32;

[[nodiscard]] QString IdToString(uint64 id) {
	return QString::number(id, 16);
}

[[nodiscard]] uint64 IdFromString(const QString &value) {
	return value.toULongLong(nullptr, 16);
}

[[nodiscard]] QString MediaTypeName(MemoMedia::Type type) {
	switch (type) {
	case MemoMedia::Type::Photo: return u"photo"_q;
	case MemoMedia::Type::File: return u"file"_q;
	case MemoMedia::Type::Voice: return u"voice"_q;
	case MemoMedia::Type::Audio: return u"audio"_q;
	case MemoMedia::Type::Video: return u"video"_q;
	}
	Unexpected("Type in Data::MediaTypeName.");
}

[[nodiscard]] MemoMedia::Type MediaTypeByName(const QString &name) {
	if (name == u"photo"_q) {
		return MemoMedia::Type::Photo;
	} else if (name == u"voice"_q) {
		return MemoMedia::Type::Voice;
	} else if (name == u"audio"_q) {
		return MemoMedia::Type::Audio;
	} else if (name == u"video"_q) {
		return MemoMedia::Type::Video;
	}
	return MemoMedia::Type::File;
}

[[nodiscard]] QJsonObject SerializeMedia(const MemoMedia &media) {
	auto result = QJsonObject{
		{ u"type"_q, MediaTypeName(media.type) },
		{ u"file"_q, media.file },
		{ u"name"_q, media.originalName },
		{ u"mime"_q, media.mime },
		{ u"size"_q, double(media.size) },
	};
	if (!media.dimensions.isEmpty()) {
		result.insert(u"width"_q, media.dimensions.width());
		result.insert(u"height"_q, media.dimensions.height());
	}
	if (media.duration) {
		result.insert(u"duration"_q, double(media.duration));
	}
	if (!media.waveform.isEmpty()) {
		result.insert(
			u"waveform"_q,
			QString::fromLatin1(media.waveform.toBase64()));
	}
	if (media.spoiler) {
		result.insert(u"spoiler"_q, true);
	}
	return result;
}

[[nodiscard]] MemoMedia DeserializeMedia(const QJsonObject &object) {
	auto result = MemoMedia{
		.type = MediaTypeByName(object[u"type"_q].toString()),
		.file = object[u"file"_q].toString(),
		.originalName = object[u"name"_q].toString(),
		.mime = object[u"mime"_q].toString(),
		.size = int64(object[u"size"_q].toDouble()),
		.duration = crl::time(object[u"duration"_q].toDouble()),
		.spoiler = object[u"spoiler"_q].toBool(),
	};
	const auto width = object[u"width"_q].toInt();
	const auto height = object[u"height"_q].toInt();
	if (width > 0 && height > 0) {
		result.dimensions = QSize(width, height);
	}
	const auto waveform = object[u"waveform"_q].toString();
	if (!waveform.isEmpty()) {
		result.waveform = QByteArray::fromBase64(waveform.toLatin1());
	}
	return result;
}

[[nodiscard]] QJsonObject SerializeMessage(const MemoMessage &message) {
	auto result = QJsonObject{
		{ u"id"_q, IdToString(message.id) },
		{ u"date"_q, double(message.date) },
		{ u"text"_q, message.text.text },
		{ u"revision"_q, double(message.revision) },
	};
	if (message.editDate) {
		result.insert(u"editDate"_q, double(message.editDate));
	}
	if (!message.text.tags.isEmpty()) {
		const auto tags = TextUtilities::SerializeTags(message.text.tags);
		result.insert(u"tags"_q, QString::fromLatin1(tags.toBase64()));
	}
	if (message.groupId) {
		result.insert(u"group"_q, IdToString(message.groupId));
	}
	if (!message.command.isEmpty()) {
		result.insert(u"command"_q, message.command);
	}
	if (message.media) {
		result.insert(u"media"_q, SerializeMedia(*message.media));
	}
	return result;
}

[[nodiscard]] MemoMessage DeserializeMessage(const QJsonObject &object) {
	auto result = MemoMessage{
		.id = IdFromString(object[u"id"_q].toString()),
		.date = TimeId(object[u"date"_q].toDouble()),
		.editDate = TimeId(object[u"editDate"_q].toDouble()),
		.groupId = IdFromString(object[u"group"_q].toString()),
		.revision = uint64(object[u"revision"_q].toDouble()),
	};
	result.text.text = object[u"text"_q].toString();
	result.command = FilterMemoCommand(object[u"command"_q].toString());
	const auto tags = object[u"tags"_q].toString();
	if (!tags.isEmpty()) {
		result.text.tags = TextUtilities::DeserializeTags(
			QByteArray::fromBase64(tags.toLatin1()),
			result.text.text.size());
	}
	const auto media = object[u"media"_q].toObject();
	if (!media.isEmpty()) {
		result.media = DeserializeMedia(media);
	}
	return result;
}

[[nodiscard]] QByteArray ReadFileContent(const QString &path) {
	auto file = QFile(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool WriteFileContent(const QString &path, const QByteArray &content) {
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		LOG(("Memo Error: Could not create '%1'.").arg(path));
		return false;
	}
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Memo Error: Could not open '%1'.").arg(path));
		return false;
	} else if (file.write(content) != content.size()) {
		file.cancelWriting();
		LOG(("Memo Error: Could not write '%1'.").arg(path));
		return false;
	} else if (!file.commit()) {
		LOG(("Memo Error: Could not commit '%1'.").arg(path));
		return false;
	}
	return true;
}

[[nodiscard]] QString FilesDir(
		not_null<Main::Session*> session,
		uint64 folderId) {
	return MemoFolderDir(session, folderId) + u"files/"_q;
}

[[nodiscard]] QString NameFromHash(
		const QByteArray &digest,
		const QString &extension) {
	const auto stem = QString::fromLatin1(digest.toHex());
	return extension.isEmpty() ? stem : (stem + '.' + extension);
}

// Media files are named by the hash of their content, so the same file
// stored twice in one folder takes one slot and keeps working when only
// one of the messages using it is removed.
[[nodiscard]] QByteArray HashContent(const QByteArray &content) {
	const auto hash = openssl::Sha256(bytes::make_span(content));
	return QByteArray(
		reinterpret_cast<const char*>(hash.data()),
		hash.size());
}

[[nodiscard]] QByteArray HashFile(const QString &path) {
	constexpr auto kChunk = 512 * 1024;
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	auto context = SHA256_CTX();
	if (!SHA256_Init(&context)) {
		return QByteArray();
	}
	while (!file.atEnd()) {
		const auto chunk = file.read(kChunk);
		if (chunk.isEmpty()) {
			break;
		}
		SHA256_Update(&context, chunk.constData(), chunk.size());
	}
	auto digest = QByteArray(SHA256_DIGEST_LENGTH, Qt::Uninitialized);
	SHA256_Final(
		reinterpret_cast<unsigned char*>(digest.data()),
		&context);
	return digest;
}

} // namespace

QString MemoRootDir(not_null<Main::Session*> session) {
	return cWorkingDir()
		+ u"tdata/memo/"_q
		+ QString::number(session->userId().bare)
		+ '/';
}

QString MemoFolderDir(not_null<Main::Session*> session, uint64 folderId) {
	return MemoRootDir(session) + IdToString(folderId) + '/';
}

QString MemoFilePath(
		not_null<Main::Session*> session,
		uint64 folderId,
		const QString &name) {
	// The name comes from the manifest, which is a plain file on disk, so
	// a corrupted or tampered one could otherwise point anywhere outside
	// of the folder and have us read or even remove an unrelated file.
	if (name.isEmpty() || QFileInfo(name).fileName() != name) {
		return QString();
	}
	return FilesDir(session, folderId) + name;
}

std::vector<MemoFolder> ReadMemoFolders(not_null<Main::Session*> session) {
	const auto content = ReadFileContent(
		MemoRootDir(session) + u"index.json"_q);
	if (content.isEmpty()) {
		return {};
	}
	const auto document = QJsonDocument::fromJson(content);
	if (!document.isObject()) {
		return {};
	}
	auto result = std::vector<MemoFolder>();
	const auto root = document.object();
	const auto list = root[u"folders"_q].toArray();
	result.reserve(list.size());
	for (const auto entry : list) {
		const auto object = entry.toObject();
		const auto id = IdFromString(object[u"id"_q].toString());
		if (!id) {
			continue;
		}
		result.push_back({
			.id = id,
			.title = object[u"title"_q].toString(),
			.order = object[u"order"_q].toInt(),
			.created = TimeId(object[u"created"_q].toDouble()),
			.updated = TimeId(object[u"updated"_q].toDouble()),
			.revision = uint64(object[u"revision"_q].toDouble()),
			.cloudId = object[u"cloudId"_q].toString(),
		});
	}
	ranges::stable_sort(result, ranges::less(), &MemoFolder::order);
	return result;
}

void WriteMemoFolders(
		not_null<Main::Session*> session,
		const std::vector<MemoFolder> &folders) {
	auto list = QJsonArray();
	for (const auto &folder : folders) {
		auto object = QJsonObject{
			{ u"id"_q, IdToString(folder.id) },
			{ u"title"_q, folder.title },
			{ u"order"_q, folder.order },
			{ u"created"_q, double(folder.created) },
			{ u"updated"_q, double(folder.updated) },
			{ u"revision"_q, double(folder.revision) },
		};
		if (!folder.cloudId.isEmpty()) {
			object.insert(u"cloudId"_q, folder.cloudId);
		}
		list.append(object);
	}
	const auto document = QJsonDocument(QJsonObject{
		{ u"version"_q, kFormatVersion },
		{ u"folders"_q, list },
	});
	WriteFileContent(
		MemoRootDir(session) + u"index.json"_q,
		document.toJson(QJsonDocument::Compact));
}

MemoManifest ReadMemoManifest(
		not_null<Main::Session*> session,
		uint64 folderId) {
	const auto content = ReadFileContent(
		MemoFolderDir(session, folderId) + u"manifest.json"_q);
	if (content.isEmpty()) {
		return {};
	}
	return ParseMemoManifest(content);
}

QByteArray SerializeMemoManifest(
		uint64 folderId,
		const MemoManifest &manifest) {
	auto list = QJsonArray();
	for (const auto &message : manifest.messages) {
		list.append(SerializeMessage(message));
	}
	return QJsonDocument(QJsonObject{
		{ u"version"_q, kFormatVersion },
		{ u"folder"_q, IdToString(folderId) },
		{ u"nextMessageId"_q, IdToString(manifest.nextMessageId) },
		{ u"messages"_q, list },
	}).toJson(QJsonDocument::Compact);
}

QByteArray SerializeMemoFolder(const MemoFolder &folder) {
	return QJsonDocument(QJsonObject{
		{ u"version"_q, kFormatVersion },
		{ u"title"_q, folder.title },
		{ u"created"_q, double(folder.created) },
		{ u"updated"_q, double(folder.updated) },
	}).toJson(QJsonDocument::Compact);
}

QString ParseMemoFolderTitle(
		const QByteArray &content,
		const QString &fallback) {
	const auto document = QJsonDocument::fromJson(content);
	const auto title = document.isObject()
		? document.object()[u"title"_q].toString()
		: QString();
	const auto filtered = FilterMemoFolderTitle(title).trimmed();
	if (GoodMemoFolderTitle(filtered)) {
		return filtered;
	}
	const auto second = FilterMemoFolderTitle(fallback).trimmed();
	return GoodMemoFolderTitle(second) ? second : u"memo"_q;
}

MemoManifest ParseMemoManifest(const QByteArray &content) {
	const auto document = QJsonDocument::fromJson(content);
	if (!document.isObject()) {
		return {};
	}
	const auto object = document.object();
	auto result = MemoManifest();
	const auto list = object[u"messages"_q].toArray();
	result.messages.reserve(list.size());
	for (const auto entry : list) {
		auto message = DeserializeMessage(entry.toObject());
		if (message.id) {
			result.messages.push_back(std::move(message));
		}
	}
	ranges::sort(result.messages, ranges::less(), &MemoMessage::id);
	const auto stored = IdFromString(object[u"nextMessageId"_q].toString());
	const auto last = result.messages.empty()
		? uint64(0)
		: result.messages.back().id;
	result.nextMessageId = std::max(stored, last + 1);
	return result;
}

void WriteMemoManifest(
		not_null<Main::Session*> session,
		uint64 folderId,
		const MemoManifest &manifest) {
	WriteFileContent(
		MemoFolderDir(session, folderId) + u"manifest.json"_q,
		SerializeMemoManifest(folderId, manifest));
}

QString CopyMemoFile(
		not_null<Main::Session*> session,
		uint64 folderId,
		const QString &sourcePath) {
	const auto directory = FilesDir(session, folderId);
	if (sourcePath.isEmpty() || !QDir().mkpath(directory)) {
		return QString();
	}
	const auto digest = HashFile(sourcePath);
	if (digest.isEmpty()) {
		return QString();
	}
	const auto name = NameFromHash(digest, QFileInfo(sourcePath).suffix());
	if (QFile::exists(directory + name)) {
		return name;
	}
	return QFile::copy(sourcePath, directory + name) ? name : QString();
}

QString SaveMemoFileContent(
		not_null<Main::Session*> session,
		uint64 folderId,
		const QByteArray &content,
		const QString &extension) {
	const auto directory = FilesDir(session, folderId);
	if (content.isEmpty() || !QDir().mkpath(directory)) {
		return QString();
	}
	const auto name = NameFromHash(HashContent(content), extension);
	if (QFile::exists(directory + name)) {
		return name;
	}
	return WriteFileContent(directory + name, content) ? name : QString();
}

void RemoveMemoFile(
		not_null<Main::Session*> session,
		uint64 folderId,
		const QString &name) {
	if (!name.isEmpty()) {
		QFile::remove(MemoFilePath(session, folderId, name));
	}
}

void RemoveMemoFolderData(
		not_null<Main::Session*> session,
		uint64 folderId) {
	QDir(MemoFolderDir(session, folderId)).removeRecursively();
}

QString FilterMemoFolderTitle(const QString &title) {
	static const auto forbidden = QString(u"\\/:*?\"<>|"_q);
	auto result = QString();
	result.reserve(title.size());
	for (const auto ch : title) {
		if (!forbidden.contains(ch) && ch.unicode() >= ' ') {
			result.append(ch);
		}
	}
	return result.left(kMaxFolderTitle);
}

bool GoodMemoFolderTitle(const QString &title) {
	static const auto reserved = base::flat_set<QString>{
		u"con"_q, u"prn"_q, u"aux"_q, u"nul"_q,
		u"com1"_q, u"com2"_q, u"com3"_q, u"com4"_q, u"com5"_q,
		u"com6"_q, u"com7"_q, u"com8"_q, u"com9"_q,
		u"lpt1"_q, u"lpt2"_q, u"lpt3"_q, u"lpt4"_q, u"lpt5"_q,
		u"lpt6"_q, u"lpt7"_q, u"lpt8"_q, u"lpt9"_q,
	};
	const auto trimmed = title.trimmed();
	return !trimmed.isEmpty()
		&& (trimmed == FilterMemoFolderTitle(trimmed))
		&& !trimmed.endsWith('.')
		&& !reserved.contains(trimmed.toLower());
}

QString FilterMemoCommand(const QString &command) {
	auto result = QString();
	result.reserve(command.size());
	for (const auto ch : command) {
		if (ch.isLetterOrNumber() || ch == '_') {
			result.append(ch);
		}
	}
	return result.left(kMaxCommandLength).toLower();
}

bool GoodMemoCommand(const QString &command) {
	return !command.isEmpty() && (command == FilterMemoCommand(command));
}

QString UniqueArchiveTitle(
		const QString &title,
		const base::flat_set<QString> &used) {
	const auto filtered = FilterMemoFolderTitle(title).trimmed();
	const auto base = (filtered.isEmpty() || !GoodMemoFolderTitle(filtered))
		? u"memo"_q
		: filtered;
	if (!used.contains(base)) {
		return base;
	}
	for (auto index = 2; index != 1000; ++index) {
		const auto candidate = base + u" ("_q + QString::number(index) + ')';
		if (!used.contains(candidate)) {
			return candidate;
		}
	}
	return base + '-' + QString::number(GenerateMemoId(), 16);
}

uint64 GenerateMemoId() {
	auto result = uint64(0);
	while (!result) {
		result = base::RandomValue<uint64>();
	}
	return result;
}

} // namespace Data
