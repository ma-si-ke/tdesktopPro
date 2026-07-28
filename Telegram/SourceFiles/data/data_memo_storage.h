/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "ui/text/text_entity.h"

namespace Main {
class Session;
} // namespace Main

namespace Data {

// Local memo storage layout, all under tdata/memo/<user id>/:
//
//     index.json                  - the folder list
//     <folder id>/manifest.json   - messages of that folder
//     <folder id>/files/<name>    - media files of that folder
//
// Every folder directory is self contained and refers to its files by
// relative name only, so that a whole folder can later be packed and
// uploaded, or downloaded and unpacked under a freshly generated id.

struct MemoFolder {
	uint64 id = 0;
	QString title;
	int order = 0;
	TimeId created = 0;
	TimeId updated = 0;
	uint64 revision = 0;
	QString cloudId;
};

struct MemoMedia {
	enum class Type : uchar {
		Photo,
		File,
		Voice,
		Audio,
		Video,
	};

	Type type = Type::File;
	QString file;
	QString originalName;
	QString mime;
	int64 size = 0;
	QSize dimensions;
	crl::time duration = 0;
	QByteArray waveform;
	bool spoiler = false;
};

struct MemoMessage {
	uint64 id = 0;
	TimeId date = 0;
	TimeId editDate = 0;
	TextWithTags text;
	uint64 groupId = 0;
	uint64 revision = 0;
	std::optional<MemoMedia> media;
};

struct MemoManifest {
	uint64 nextMessageId = 1;
	std::vector<MemoMessage> messages;
};

[[nodiscard]] QString MemoRootDir(not_null<Main::Session*> session);
[[nodiscard]] QString MemoFolderDir(
	not_null<Main::Session*> session,
	uint64 folderId);
[[nodiscard]] QString MemoFilePath(
	not_null<Main::Session*> session,
	uint64 folderId,
	const QString &name);

[[nodiscard]] std::vector<MemoFolder> ReadMemoFolders(
	not_null<Main::Session*> session);
void WriteMemoFolders(
	not_null<Main::Session*> session,
	const std::vector<MemoFolder> &folders);

[[nodiscard]] MemoManifest ReadMemoManifest(
	not_null<Main::Session*> session,
	uint64 folderId);
void WriteMemoManifest(
	not_null<Main::Session*> session,
	uint64 folderId,
	const MemoManifest &manifest);

[[nodiscard]] QString CopyMemoFile(
	not_null<Main::Session*> session,
	uint64 folderId,
	const QString &sourcePath);
[[nodiscard]] QString SaveMemoFileContent(
	not_null<Main::Session*> session,
	uint64 folderId,
	const QByteArray &content,
	const QString &extension);
void RemoveMemoFile(
	not_null<Main::Session*> session,
	uint64 folderId,
	const QString &name);
void RemoveMemoFolderData(
	not_null<Main::Session*> session,
	uint64 folderId);

[[nodiscard]] uint64 GenerateMemoId();

} // namespace Data
