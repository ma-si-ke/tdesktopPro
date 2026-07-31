/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "plugins/plugin_market.h"

#include "base/debug_log.h"
#include "base/openssl_help.h"
#include "base/zlib_help.h"
#include "plugins/plugin_manifest.h"
#include "plugins/plugin_registry.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>
#include <QtCore/QTemporaryFile>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>

namespace Plugins {
namespace {

constexpr auto kListUrl = "https://td.kakaco.top/api/plugins";
constexpr auto kListTimeout = 20 * 1000;
constexpr auto kDownloadTimeout = 5 * 60 * 1000;
constexpr auto kMaxListSize = 1024 * 1024;
constexpr auto kMaxPackageSize = int64(64 * 1024 * 1024);
constexpr auto kMaxEntrySize = int64(128 * 1024 * 1024);
constexpr auto kMaxEntries = 256;
constexpr auto kMaxNameSize = 1024;
constexpr auto kChunk = 512 * 1024;

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
		&& (result.size <= kMaxPackageSize);
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

// A name coming from the archive may point anywhere, so only plain
// entries below the destination are accepted.
[[nodiscard]] bool GoodEntryName(const QString &name) {
	if (name.isEmpty() || name.size() > kMaxNameSize) {
		return false;
	} else if (name.contains(u".."_q)
		|| name.contains('\\')
		|| name.startsWith('/')
		|| name.contains(':')) {
		return false;
	}
	for (const auto &part : name.split('/')) {
		if (part == u"."_q || part == u".."_q) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool ExtractCurrentEntry(
		unzFile handle,
		const QString &path,
		int64 size) {
	if (size < 0 || size > kMaxEntrySize) {
		return false;
	} else if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		return false;
	} else if (unzOpenCurrentFile(handle) != UNZ_OK) {
		return false;
	}
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		unzCloseCurrentFile(handle);
		return false;
	}
	auto buffer = QByteArray(kChunk, Qt::Uninitialized);
	auto ok = true;
	while (ok) {
		const auto read = unzReadCurrentFile(handle, buffer.data(), kChunk);
		if (read < 0) {
			ok = false;
		} else if (!read) {
			break;
		} else {
			ok = (file.write(buffer.constData(), read) == read);
		}
	}
	file.close();
	unzCloseCurrentFile(handle);
	if (!ok) {
		QFile::remove(path);
	}
	return ok;
}

[[nodiscard]] bool ExtractPackage(
		const QString &archive,
		const QString &destination) {
	const auto utf8 = QFile::encodeName(archive);
	const auto handle = unzOpen(utf8.constData());
	if (!handle) {
		return false;
	}
	const auto guard = gsl::finally([&] { unzClose(handle); });
	if (!QDir().mkpath(destination)) {
		return false;
	}
	auto ok = (unzGoToFirstFile(handle) == UNZ_OK);
	auto count = 0;
	while (ok) {
		if (++count > kMaxEntries) {
			return false;
		}
		auto info = unz_file_info();
		auto nameBuffer = std::array<char, kMaxNameSize>();
		if (unzGetCurrentFileInfo(
			handle,
			&info,
			nameBuffer.data(),
			nameBuffer.size(),
			nullptr,
			0,
			nullptr,
			0) != UNZ_OK) {
			return false;
		}
		const auto name = QString::fromUtf8(nameBuffer.data());
		if (!GoodEntryName(name)) {
			LOG(("Plugins Error: Bad entry '%1' in a package.").arg(name));
			return false;
		} else if (!name.endsWith('/')) {
			if (!ExtractCurrentEntry(
				handle,
				destination + '/' + name,
				int64(info.uncompressed_size))) {
				return false;
			}
		}
		const auto next = unzGoToNextFile(handle);
		if (next == UNZ_END_OF_LIST_OF_FILE) {
			break;
		}
		ok = (next == UNZ_OK);
	}
	return ok;
}

// The package must describe the very plugin it was downloaded for.
[[nodiscard]] bool GoodPackage(
		const QString &folder,
		const QString &name,
		int version) {
	auto file = QFile(folder + u"/plugin.json"_q);
	if (!file.open(QIODevice::ReadOnly)) {
		LOG(("Plugins Error: No manifest in the package of '%1'.").arg(name));
		return false;
	}
	const auto manifest = ParseManifest(file.readAll(), name);
	if (!manifest) {
		return false;
	} else if (manifest->version != version) {
		LOG(("Plugins Error: Package of '%1' declares version %2, not %3."
			).arg(name).arg(manifest->version).arg(version));
		return false;
	} else if (!QFile::exists(folder + '/' + manifest->library)) {
		LOG(("Plugins Error: Library missing in the package of '%1'."
			).arg(name));
		return false;
	}
	return true;
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
	const auto folder = asUpdate
		? PluginUpdateDir(name)
		: PluginDir(name);

	auto request = QNetworkRequest(QUrl(entry.link));
	request.setTransferTimeout(kDownloadTimeout);
	const auto reply = Manager().get(request);
	if (progress) {
		QObject::connect(
			reply,
			&QNetworkReply::downloadProgress,
			[=](qint64 received, qint64 total) {
				const auto full = (total > 0) ? total : expectedSize;
				progress(full > 0 ? int((received * 100) / full) : 0);
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
				).arg(name).arg(content.size()).arg(expectedSize));
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

		// minizip works on files, so the verified bytes go through a
		// temporary of their own before being extracted.
		auto archive = QTemporaryFile(PluginsDir() + u"package.XXXXXX"_q);
		archive.setAutoRemove(true);
		if (!archive.open()
			|| archive.write(content) != content.size()
			|| !archive.flush()) {
			done({ .error = u"写入失败"_q });
			return;
		}
		const auto archivePath = archive.fileName();
		archive.close();

		auto directory = QDir(folder);
		if (directory.exists() && !directory.removeRecursively()) {
			done({ .error = u"无法清理旧文件"_q });
			return;
		}
		if (!ExtractPackage(archivePath, folder)
			|| !GoodPackage(folder, name, version)) {
			QDir(folder).removeRecursively();
			done({ .error = u"插件包无效"_q });
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
