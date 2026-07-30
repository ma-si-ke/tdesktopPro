/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "plugins/plugin_market.h"

#include "base/debug_log.h"
#include "base/openssl_help.h"
#include "plugins/plugin_registry.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

namespace Plugins {
namespace {

constexpr auto kListUrl = "https://td.kakaco.top/api/plugins";
constexpr auto kListTimeout = 20 * 1000;
constexpr auto kDownloadTimeout = 5 * 60 * 1000;
constexpr auto kMaxListSize = 1024 * 1024;
constexpr auto kMaxPluginSize = int64(64 * 1024 * 1024);

[[nodiscard]] QNetworkAccessManager &Manager() {
	static auto result = QNetworkAccessManager();
	return result;
}

[[nodiscard]] bool GoodHash(const QString &hash) {
	if (hash.size() != 64) {
		return false;
	}
	for (const auto &ch : hash) {
		const auto good = (ch >= '0' && ch <= '9')
			|| (ch >= 'a' && ch <= 'f')
			|| (ch >= 'A' && ch <= 'F');
		if (!good) {
			return false;
		}
	}
	return true;
}

// A single bad entry is skipped, it must not hide the whole market.
[[nodiscard]] std::optional<MarketEntry> ParseEntry(
		const QJsonValue &value) {
	if (!value.isObject()) {
		return std::nullopt;
	}
	const auto object = value.toObject();
	auto result = MarketEntry();
	result.name = object.value(u"name"_q).toString();
	result.version = int(object.value(u"version"_q).toDouble());
	result.link = object.value(u"link"_q).toString();
	result.sha256 = object.value(u"sha256"_q).toString().toLower();
	result.size = int64(object.value(u"size"_q).toDouble());
	result.releasedAt = object.value(u"released_at"_q).toString();
	result.description = object.value(u"description"_q).toString();
	const auto good = GoodPluginName(result.name)
		&& (result.version > 0)
		&& (result.link.startsWith(u"https://"_q)
			|| result.link.startsWith(u"http://"_q))
		&& GoodHash(result.sha256)
		&& (result.size > 0)
		&& (result.size <= kMaxPluginSize);
	if (!good) {
		LOG(("Plugins Error: Skipping a bad market entry '%1'."
			).arg(result.name));
		return std::nullopt;
	}
	return result;
}

[[nodiscard]] QString HashOf(const QByteArray &content) {
	const auto hash = openssl::Sha256(bytes::make_span(content));
	const auto bytes = QByteArray(
		reinterpret_cast<const char*>(hash.data()),
		hash.size());
	return QString::fromLatin1(bytes.toHex());
}

} // namespace

void RequestMarket(Fn<void(MarketList)> done) {
	auto request = QNetworkRequest(QUrl(QString::fromLatin1(kListUrl)));
	request.setTransferTimeout(kListTimeout);
	const auto reply = Manager().get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto guard = gsl::finally([&] { reply->deleteLater(); });
		if (reply->error() != QNetworkReply::NoError) {
			LOG(("Plugins Error: Market request failed %1."
				).arg(int(reply->error())));
			done({ .failed = true });
			return;
		}
		const auto bytes = reply->readAll();
		if (bytes.isEmpty() || bytes.size() > kMaxListSize) {
			done({ .failed = true });
			return;
		}
		const auto document = QJsonDocument::fromJson(bytes);
		if (!document.isObject()) {
			LOG(("Plugins Error: Market answer is not an object."));
			done({ .failed = true });
			return;
		}
		const auto plugins = document.object().value(u"plugins"_q);
		if (!plugins.isArray()) {
			LOG(("Plugins Error: Market answer has no plugins array."));
			done({ .failed = true });
			return;
		}
		auto result = MarketList();
		for (const auto &value : plugins.toArray()) {
			if (auto entry = ParseEntry(value)) {
				result.entries.push_back(std::move(*entry));
			}
		}
		done(std::move(result));
	});
}

void Download(
		const MarketEntry &entry,
		bool asUpdate,
		Fn<void(int)> progress,
		Fn<void(DownloadResult)> done) {
	const auto name = entry.name;
	const auto version = entry.version;
	const auto expectedHash = entry.sha256;
	const auto expectedSize = entry.size;
	const auto path = asUpdate
		? PluginUpdatePath(name)
		: PluginPath(name);

	auto request = QNetworkRequest(QUrl(entry.link));
	request.setTransferTimeout(kDownloadTimeout);
	const auto reply = Manager().get(request);
	if (progress) {
		QObject::connect(
			reply,
			&QNetworkReply::downloadProgress,
			[=](qint64 received, qint64 total) {
				const auto full = (total > 0) ? total : expectedSize;
				progress(full > 0
					? int((received * 100) / full)
					: 0);
			});
	}
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		const auto guard = gsl::finally([&] { reply->deleteLater(); });
		if (reply->error() != QNetworkReply::NoError) {
			done({ .error = u"下载失败"_q });
			return;
		}
		const auto content = reply->readAll();
		if (content.size() != expectedSize) {
			LOG(("Plugins Error: '%1' size %2, expected %3."
				).arg(name
				).arg(content.size()
				).arg(expectedSize));
			done({ .error = u"文件大小不符"_q });
			return;
		} else if (HashOf(content) != expectedHash) {
			LOG(("Plugins Error: '%1' hash mismatch.").arg(name));
			done({ .error = u"校验失败"_q });
			return;
		} else if (!QDir().mkpath(PluginsDir())) {
			done({ .error = u"无法创建插件目录"_q });
			return;
		}
		auto file = QSaveFile(path);
		if (!file.open(QIODevice::WriteOnly)
			|| file.write(content) != content.size()
			|| !file.commit()) {
			LOG(("Plugins Error: Could not write '%1'.").arg(path));
			done({ .error = u"写入失败"_q });
			return;
		}
		if (asUpdate) {
			RegisterPendingUpdate(name, version);
		} else {
			RegisterInstalled(name, version);
		}
		done({ .ok = true, .asUpdate = asUpdate });
	});
}

} // namespace Plugins
