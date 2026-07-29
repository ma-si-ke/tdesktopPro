/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Memo {

// Packing and unpacking happen on a worker thread, the callbacks are
// invoked on the main thread. Nothing here may touch the session or any
// widget, so a folder is described by plain data only.
//
// An archive holds one directory per folder at its root:
//
//     <folder title>/folder.json
//     <folder title>/manifest.json
//     <folder title>/files/<name>

struct ArchiveFolder {
	QString title;
	QByteArray folder;
	QByteArray manifest;
	std::vector<std::pair<QString, QString>> files;
};

struct UnpackedFolder {
	QString title;
	QByteArray folder;
	QByteArray manifest;
	QString directory;
};

void Pack(
	std::vector<ArchiveFolder> folders,
	QString path,
	Fn<void(QString error)> done);

void Unpack(
	QString path,
	QString temporary,
	Fn<void(std::vector<UnpackedFolder> folders, QString error)> done);

void ClearTemporary(QString temporary);

} // namespace Memo
