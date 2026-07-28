/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_memo_storage.h"

#include "base/random.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Data {
namespace {

constexpr auto kFormatVersion = 1;

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
		return false;
	}
	const auto temporary = path + u".tmp"_q;
	auto file = QFile(temporary);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	} else if (file.write(content) != content.size()) {
		file.close();
		QFile::remove(temporary);
		return false;
	}
	file.close();
	QFile::remove(path);
	return QFile::rename(temporary, path);
}

[[nodiscard]] QString FilesDir(
		not_null<Main::Session*> session,
		uint64 folderId) {
	return MemoFolderDir(session, folderId) + u"files/"_q;
}

[[nodiscard]] QString GenerateFileName(const QString &extension) {
	const auto stem = QString::number(GenerateMemoId(), 16);
	return extension.isEmpty() ? stem : (stem + '.' + extension);
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
	const auto list = document.object()[u"folders"_q].toArray();
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
	ranges::sort(result, ranges::less(), &MemoFolder::order);
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
	auto list = QJsonArray();
	for (const auto &message : manifest.messages) {
		list.append(SerializeMessage(message));
	}
	const auto document = QJsonDocument(QJsonObject{
		{ u"version"_q, kFormatVersion },
		{ u"folder"_q, IdToString(folderId) },
		{ u"nextMessageId"_q, IdToString(manifest.nextMessageId) },
		{ u"messages"_q, list },
	});
	WriteFileContent(
		MemoFolderDir(session, folderId) + u"manifest.json"_q,
		document.toJson(QJsonDocument::Compact));
}

QString CopyMemoFile(
		not_null<Main::Session*> session,
		uint64 folderId,
		const QString &sourcePath) {
	const auto directory = FilesDir(session, folderId);
	if (sourcePath.isEmpty() || !QDir().mkpath(directory)) {
		return QString();
	}
	const auto name = GenerateFileName(QFileInfo(sourcePath).suffix());
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
	const auto name = GenerateFileName(extension);
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

uint64 GenerateMemoId() {
	auto result = uint64(0);
	while (!result) {
		result = base::RandomValue<uint64>();
	}
	return result;
}

} // namespace Data
