/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "cloudmemo/cloud_memo.h"

#include "base/flat_map.h"
#include "base/flat_set.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "logs.h"
#include "settings.h"

#include <rpl/event_stream.h>

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QImage>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace CloudMemo {
namespace {

constexpr auto kBaseUrlPrefKey = "cloud_memo/base_url";
constexpr auto kUserId = "global";

struct MemoChange {
	QString tgId;
	Memo memo;
};

[[nodiscard]] rpl::event_stream<MemoChange> &MemoChanges() {
	static auto result = rpl::event_stream<MemoChange>();
	return result;
}

[[nodiscard]] base::flat_map<QString, Memo> &MemoryCache() {
	static auto result = base::flat_map<QString, Memo>();
	return result;
}

[[nodiscard]] base::flat_set<QString> &FetchingNow() {
	static auto result = base::flat_set<QString>();
	return result;
}

[[nodiscard]] base::flat_map<QString, std::vector<Fn<void(QImage)>>> &
PhotoLoadsNow() {
	static auto result
		= base::flat_map<QString, std::vector<Fn<void(QImage)>>>();
	return result;
}

[[nodiscard]] not_null<QNetworkAccessManager*> Network() {
	static const auto result = new QNetworkAccessManager();
	return result;
}

[[nodiscard]] QString CacheDir() {
	return cWorkingDir() + u"tdata/cloud_memo"_q;
}

[[nodiscard]] QString MemoCachePath(const QString &tgId) {
	return CacheDir() + u"/memos/"_q + tgId + u".json"_q;
}

[[nodiscard]] QUrl Endpoint(const QString &path) {
	return QUrl(BaseUrl() + path);
}

[[nodiscard]] QNetworkRequest JsonRequest(const QString &path) {
	auto request = QNetworkRequest(Endpoint(path));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	return request;
}

void Finished(not_null<QNetworkReply*> reply, Fn<void(int, QByteArray)> done) {
	QObject::connect(
		reply.get(),
		&QNetworkReply::finished,
		reply.get(),
		[reply = reply.get(), done = std::move(done)] {
			const auto status = reply
				->attribute(QNetworkRequest::HttpStatusCodeAttribute)
				.toInt();
			const auto body = reply->readAll();
			reply->deleteLater();
			done(status, body);
		});
}

[[nodiscard]] Memo ParseMemoView(const QByteArray &body) {
	auto result = Memo();
	const auto document = QJsonDocument::fromJson(body);
	if (!document.isObject()) {
		return result;
	}
	const auto object = document.object();
	result.text = object[u"text"_q].toString();
	for (const auto photo : object[u"photos"_q].toArray()) {
		if (const auto id = photo.toObject()[u"id"_q].toString()
			; !id.isEmpty()) {
			result.photoIds.push_back(id);
		}
	}
	return result;
}

void StoreCache(const QString &tgId, const Memo &memo, bool fire) {
	MemoryCache()[tgId] = memo;
	if (QDir().mkpath(CacheDir() + u"/memos"_q)) {
		auto photoIds = QJsonArray();
		for (const auto &id : memo.photoIds) {
			photoIds.append(id);
		}
		auto file = QFile(MemoCachePath(tgId));
		if (file.open(QIODevice::WriteOnly)) {
			file.write(QJsonDocument(QJsonObject{
				{ u"text"_q, memo.text },
				{ u"photoIds"_q, photoIds },
			}).toJson(QJsonDocument::Compact));
		}
	}
	if (fire) {
		MemoChanges().fire({ tgId, memo });
	}
}

[[nodiscard]] Memo ReadDiskCache(const QString &tgId) {
	auto file = QFile(MemoCachePath(tgId));
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		return {};
	}
	const auto object = document.object();
	auto result = Memo{ .text = object[u"text"_q].toString() };
	for (const auto id : object[u"photoIds"_q].toArray()) {
		if (const auto value = id.toString(); !value.isEmpty()) {
			result.photoIds.push_back(value);
		}
	}
	return result;
}

} // namespace

QString PhotoCachePath(const QString &photoId) {
	return CacheDir() + u"/photos/"_q + photoId;
}

QString DefaultBaseUrl() {
	return u"http://43.132.171.63:3100"_q;
}

QString BaseUrl() {
	auto url = Core::App().settings().readPref<QString>(kBaseUrlPrefKey);
	url = url.trimmed();
	if (!url.startsWith(u"http://"_q) && !url.startsWith(u"https://"_q)) {
		url = DefaultBaseUrl();
	}
	while (url.endsWith('/')) {
		url.chop(1);
	}
	return url;
}

void SetBaseUrl(const QString &url) {
	Core::App().settings().writePref<QString>(
		kBaseUrlPrefKey,
		url.trimmed());
}

Memo Cached(const QString &tgId) {
	const auto i = MemoryCache().find(tgId);
	if (i != end(MemoryCache())) {
		return i->second;
	}
	auto result = ReadDiskCache(tgId);
	MemoryCache()[tgId] = result;
	return result;
}

rpl::producer<Memo> MemoValue(const QString &tgId) {
	return rpl::single(
		Cached(tgId)
	) | rpl::then(MemoChanges().events(
	) | rpl::filter([=](const MemoChange &change) {
		return change.tgId == tgId;
	}) | rpl::map([](const MemoChange &change) {
		return change.memo;
	}));
}

void Fetch(const QString &tgId) {
	if (!FetchingNow().emplace(tgId).second) {
		return;
	}
	const auto path = u"/api/v1/memos/"_q + kUserId + '/' + tgId;
	const auto reply = Network()->get(QNetworkRequest(Endpoint(path)));
	Finished(reply, [=](int status, const QByteArray &body) {
		FetchingNow().remove(tgId);
		if (status < 200 || status >= 300) {
			LOG(("CloudMemo: fetch %1 http=%2").arg(tgId).arg(status));
			return;
		}
		StoreCache(tgId, ParseMemoView(body), true);
	});
}

void Write(
		const QString &tgId,
		Memo memo,
		Fn<void(QString error)> done) {
	auto photoIds = QJsonArray();
	for (const auto &id : memo.photoIds) {
		photoIds.append(id);
	}
	const auto payload = QJsonDocument(QJsonObject{
		{ u"text"_q, memo.text },
		{ u"photoIds"_q, photoIds },
	}).toJson(QJsonDocument::Compact);
	const auto path = u"/api/v1/memos/"_q + kUserId + '/' + tgId;
	const auto reply = Network()->put(JsonRequest(path), payload);
	Finished(reply, [=, memo = std::move(memo)](
			int status,
			const QByteArray &body) {
		if (status < 200 || status >= 300) {
			LOG(("CloudMemo: write %1 http=%2").arg(tgId).arg(status));
			const auto error = QJsonDocument::fromJson(body)
				.object()[u"message"_q].toString();
			done(error.isEmpty() ? u"HTTP %1"_q.arg(status) : error);
			return;
		}
		StoreCache(tgId, memo, true);
		done(QString());
	});
}

void UploadPhoto(
		const QByteArray &bytes,
		const QString &mime,
		Fn<void(QString photoId)> done) {
	if (bytes.isEmpty()) {
		done(QString());
		return;
	}
	const auto photoId = QString::fromLatin1(
		QCryptographicHash::hash(
			bytes,
			QCryptographicHash::Sha256).toHex());
	auto request = QNetworkRequest(Endpoint(u"/api/v1/photos/"_q + photoId));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		mime.isEmpty() ? u"application/octet-stream"_q : mime);
	const auto reply = Network()->put(request, bytes);
	Finished(reply, [=](int status, const QByteArray &body) {
		if (status < 200 || status >= 300) {
			LOG(("CloudMemo: upload http=%1").arg(status));
			done(QString());
			return;
		}
		if (QDir().mkpath(CacheDir() + u"/photos"_q)) {
			auto file = QFile(PhotoCachePath(photoId));
			if (file.open(QIODevice::WriteOnly)) {
				file.write(bytes);
			}
		}
		done(photoId);
	});
}

void LoadPhoto(const QString &photoId, Fn<void(QImage image)> done) {
	if (photoId.isEmpty()) {
		done(QImage());
		return;
	}
	const auto cached = QImage(PhotoCachePath(photoId));
	if (!cached.isNull()) {
		done(cached);
		return;
	}
	auto &loads = PhotoLoadsNow();
	const auto i = loads.find(photoId);
	if (i != end(loads)) {
		i->second.push_back(std::move(done));
		return;
	}
	loads[photoId].push_back(std::move(done));
	const auto reply = Network()->get(
		QNetworkRequest(Endpoint(u"/api/v1/photos/"_q + photoId)));
	Finished(reply, [=](int status, const QByteArray &body) {
		auto callbacks = std::vector<Fn<void(QImage)>>();
		if (const auto i = PhotoLoadsNow().find(photoId)
			; i != end(PhotoLoadsNow())) {
			callbacks = std::move(i->second);
			PhotoLoadsNow().erase(i);
		}
		auto image = QImage();
		if (status >= 200 && status < 300 && !body.isEmpty()) {
			image = QImage::fromData(body);
			if (!image.isNull()
				&& QDir().mkpath(CacheDir() + u"/photos"_q)) {
				auto file = QFile(PhotoCachePath(photoId));
				if (file.open(QIODevice::WriteOnly)) {
					file.write(body);
				}
			}
		} else {
			LOG(("CloudMemo: photo %1 http=%2").arg(photoId).arg(status));
		}
		for (const auto &callback : callbacks) {
			callback(image);
		}
	});
}

} // namespace CloudMemo
