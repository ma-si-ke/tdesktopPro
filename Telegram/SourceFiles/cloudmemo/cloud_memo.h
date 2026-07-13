/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <rpl/producer.h>

#include <QtGui/QImage>

// Client for the standalone CloudMemo backend: notes about a Telegram
// peer shared between everyone using this client. Unauthenticated, all
// clients write under one fixed userId, last write wins. Memos and
// photo binaries are cached on disk under tdata/cloud_memo/ so notes
// stay visible offline. Not related to the local-only notes in
// data/data_local_notes.h.

namespace CloudMemo {

struct Memo {
	QString text;
	QStringList photoIds;

	[[nodiscard]] bool empty() const {
		return text.isEmpty() && photoIds.isEmpty();
	}
};

[[nodiscard]] QString DefaultBaseUrl();
[[nodiscard]] QString BaseUrl();
void SetBaseUrl(const QString &url);

[[nodiscard]] Memo Cached(const QString &tgId);
[[nodiscard]] rpl::producer<Memo> MemoValue(const QString &tgId);
void Fetch(const QString &tgId);
void Write(
	const QString &tgId,
	Memo memo,
	Fn<void(QString error)> done);
void UploadPhoto(
	const QByteArray &bytes,
	const QString &mime,
	Fn<void(QString photoId)> done);
void LoadPhoto(const QString &photoId, Fn<void(QImage image)> done);
[[nodiscard]] QString PhotoCachePath(const QString &photoId);

} // namespace CloudMemo
