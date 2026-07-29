/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "memo/memo_archive.h"

#include "base/zlib_help.h"
#include "lang/lang_keys.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace Memo {
namespace {

constexpr auto kChunk = 512 * 1024;
constexpr auto kMaxEntrySize = int64(4ULL * 1024 * 1024 * 1024);
constexpr auto kMaxNameSize = 1024;

[[nodiscard]] QString FolderEntry() {
	return u"folder.json"_q;
}

[[nodiscard]] QString ManifestEntry() {
	return u"manifest.json"_q;
}

[[nodiscard]] QString FilesEntry() {
	return u"files/"_q;
}

[[nodiscard]] bool WriteEntry(
		zipFile handle,
		const QString &name,
		const QByteArray &content,
		bool compress) {
	const auto utf8 = name.toUtf8();
	if (zipOpenNewFileInZip(
		handle,
		utf8.constData(),
		nullptr,
		nullptr,
		0,
		nullptr,
		0,
		nullptr,
		compress ? Z_DEFLATED : 0,
		compress ? Z_DEFAULT_COMPRESSION : 0) != ZIP_OK) {
		return false;
	}
	const auto ok = content.isEmpty()
		|| (zipWriteInFileInZip(
			handle,
			content.constData(),
			content.size()) == ZIP_OK);
	return (zipCloseFileInZip(handle) == ZIP_OK) && ok;
}

[[nodiscard]] bool WriteFileEntry(
		zipFile handle,
		const QString &name,
		const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}
	const auto utf8 = name.toUtf8();
	// Media is already compressed, deflating it again only costs time.
	if (zipOpenNewFileInZip(
		handle,
		utf8.constData(),
		nullptr,
		nullptr,
		0,
		nullptr,
		0,
		nullptr,
		0,
		0) != ZIP_OK) {
		return false;
	}
	auto ok = true;
	while (ok && !file.atEnd()) {
		const auto chunk = file.read(kChunk);
		if (chunk.isEmpty()) {
			break;
		}
		ok = (zipWriteInFileInZip(
			handle,
			chunk.constData(),
			chunk.size()) == ZIP_OK);
	}
	return (zipCloseFileInZip(handle) == ZIP_OK) && ok;
}

// A name coming from the archive may point anywhere, so only plain
// entries inside the expected layout are accepted.
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

[[nodiscard]] QByteArray ReadCurrentEntry(unzFile handle, int64 size) {
	if (size <= 0 || size > kMaxEntrySize) {
		return QByteArray();
	} else if (unzOpenCurrentFile(handle) != UNZ_OK) {
		return QByteArray();
	}
	auto result = QByteArray(int(size), Qt::Uninitialized);
	const auto read = unzReadCurrentFile(handle, result.data(), uInt(size));
	unzCloseCurrentFile(handle);
	return (read == int(size)) ? result : QByteArray();
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

[[nodiscard]] bool PackSync(
		const std::vector<ArchiveFolder> &folders,
		const QString &path) {
	const auto utf8 = QFile::encodeName(path);
	const auto handle = zipOpen(utf8.constData(), APPEND_STATUS_CREATE);
	if (!handle) {
		return false;
	}
	auto failed = false;
	for (const auto &folder : folders) {
		const auto root = folder.title + '/';
		if (!WriteEntry(handle, root + FolderEntry(), folder.folder, true)
			|| !WriteEntry(
				handle,
				root + ManifestEntry(),
				folder.manifest,
				true)) {
			failed = true;
			break;
		}
		for (const auto &[name, source] : folder.files) {
			if (!WriteFileEntry(handle, root + FilesEntry() + name, source)) {
				failed = true;
				break;
			}
		}
		if (failed) {
			break;
		}
	}
	if (zipClose(handle, nullptr) != ZIP_OK) {
		failed = true;
	}
	if (failed) {
		QFile::remove(path);
		return false;
	}
	return true;
}

[[nodiscard]] std::vector<UnpackedFolder> UnpackSync(
		const QString &path,
		const QString &temporary,
		bool &ok) {
	auto result = std::vector<UnpackedFolder>();
	const auto utf8 = QFile::encodeName(path);
	const auto handle = unzOpen(utf8.constData());
	if (!handle) {
		ok = false;
		return result;
	}
	auto folders = base::flat_map<QString, UnpackedFolder>();
	ok = (unzGoToFirstFile(handle) == UNZ_OK);
	while (ok) {
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
			ok = false;
			break;
		}
		const auto name = QString::fromUtf8(nameBuffer.data());
		if (GoodEntryName(name) && !name.endsWith('/')) {
			const auto slash = name.indexOf('/');
			if (slash > 0) {
				const auto title = name.left(slash);
				const auto rest = name.mid(slash + 1);
				auto &folder = folders[title];
				folder.title = title;
				if (rest == FolderEntry()) {
					folder.folder = ReadCurrentEntry(handle, info.uncompressed_size);
				} else if (rest == ManifestEntry()) {
					folder.manifest = ReadCurrentEntry(
						handle,
						info.uncompressed_size);
				} else if (rest.startsWith(FilesEntry())) {
					const auto file = rest.mid(FilesEntry().size());
					if (!file.isEmpty() && !file.contains('/')) {
						if (folder.directory.isEmpty()) {
							folder.directory = temporary
								+ QString::number(folders.size())
								+ '/';
						}
						if (!ExtractCurrentEntry(
							handle,
							folder.directory + file,
							info.uncompressed_size)) {
							ok = false;
							break;
						}
					}
				}
			}
		}
		const auto next = unzGoToNextFile(handle);
		if (next == UNZ_END_OF_LIST_OF_FILE) {
			break;
		} else if (next != UNZ_OK) {
			ok = false;
		}
	}
	unzClose(handle);
	if (!ok) {
		return result;
	}
	for (auto &[title, folder] : folders) {
		if (!folder.manifest.isEmpty()) {
			result.push_back(std::move(folder));
		}
	}
	return result;
}

} // namespace

void Pack(
		std::vector<ArchiveFolder> folders,
		QString path,
		Fn<void(QString error)> done) {
	crl::async([
		folders = std::move(folders),
		path = std::move(path),
		done = std::move(done)
	]() mutable {
		const auto ok = PackSync(folders, path);
		crl::on_main([done = std::move(done), ok] {
			done(ok ? QString() : tr::lng_memo_archive_error(tr::now));
		});
	});
}

void Unpack(
		QString path,
		QString temporary,
		Fn<void(std::vector<UnpackedFolder> folders, QString error)> done) {
	crl::async([
		path = std::move(path),
		temporary = std::move(temporary),
		done = std::move(done)
	]() mutable {
		auto ok = true;
		auto folders = UnpackSync(path, temporary, ok);
		crl::on_main([
			done = std::move(done),
			folders = std::move(folders),
			ok
		]() mutable {
			const auto error = !ok
				? tr::lng_memo_archive_error(tr::now)
				: folders.empty()
				? tr::lng_memo_archive_empty(tr::now)
				: QString();
			done(std::move(folders), error);
		});
	});
}

void ClearTemporary(QString temporary) {
	crl::async([temporary = std::move(temporary)] {
		QDir(temporary).removeRecursively();
	});
}

} // namespace Memo
