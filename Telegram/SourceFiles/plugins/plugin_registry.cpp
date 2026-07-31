/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "plugins/plugin_registry.h"

#include "base/debug_log.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Plugins {
namespace {

constexpr auto kMaxNameLength = 64;

struct State {
	std::vector<Installed> list;
	bool loaded = false;
	rpl::event_stream<> changes;
};

[[nodiscard]] State &Data() {
	static auto result = State();
	return result;
}

[[nodiscard]] Installed *Lookup(const QString &name) {
	auto &list = Data().list;
	const auto i = ranges::find(list, name, &Installed::name);
	return (i != end(list)) ? &*i : nullptr;
}

[[nodiscard]] QString RegistryPath() {
	return cWorkingDir() + u"tdata/plugins.json"_q;
}

void Save() {
	auto array = QJsonArray();
	for (const auto &entry : Data().list) {
		auto object = QJsonObject();
		object.insert(u"name"_q, entry.name);
		object.insert(u"version"_q, entry.version);
		if (entry.deleted) {
			object.insert(u"deleted"_q, true);
		}
		if (entry.pendingVersion) {
			object.insert(u"pendingVersion"_q, entry.pendingVersion);
		}
		array.append(object);
	}
	auto root = QJsonObject();
	root.insert(u"installed"_q, array);
	const auto content = QJsonDocument(root).toJson(QJsonDocument::Indented);

	const auto path = RegistryPath();
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)) {
		LOG(("Plugins Error: Could not open '%1'.").arg(path));
		return;
	} else if (file.write(content) != content.size()) {
		file.cancelWriting();
		LOG(("Plugins Error: Could not write '%1'.").arg(path));
		return;
	} else if (!file.commit()) {
		LOG(("Plugins Error: Could not commit '%1'.").arg(path));
	}
}

void Load() {
	auto &data = Data();
	if (data.loaded) {
		return;
	}
	data.loaded = true;

	auto file = QFile(RegistryPath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (!document.isObject()) {
		LOG(("Plugins Error: Bad registry, starting empty."));
		return;
	}
	const auto array = document.object().value(u"installed"_q).toArray();
	for (const auto &value : array) {
		if (!value.isObject()) {
			continue;
		}
		const auto object = value.toObject();
		auto entry = Installed();
		entry.name = object.value(u"name"_q).toString();
		if (!GoodPluginName(entry.name) || Lookup(entry.name)) {
			continue;
		}
		entry.version = int(object.value(u"version"_q).toDouble());
		entry.deleted = object.value(u"deleted"_q).toBool();
		entry.pendingVersion = int(
			object.value(u"pendingVersion"_q).toDouble());
		data.list.push_back(entry);
	}
}

void RemoveFolder(const QString &path) {
	auto dir = QDir(path);
	if (dir.exists() && !dir.removeRecursively()) {
		LOG(("Plugins Error: Could not remove '%1'.").arg(path));
	}
}

} // namespace

bool GoodPluginName(const QString &name) {
	if (name.isEmpty() || name.size() > kMaxNameLength) {
		return false;
	}
	for (const auto &ch : name) {
		const auto good = (ch >= 'a' && ch <= 'z')
			|| (ch >= '0' && ch <= '9')
			|| (ch == '_')
			|| (ch == '-');
		if (!good) {
			return false;
		}
	}
	return true;
}

QString PluginsDir() {
	return QCoreApplication::applicationDirPath() + u"/plugins/"_q;
}

QString PluginDir(const QString &name) {
	return PluginsDir() + name;
}

QString PluginUpdateDir(const QString &name) {
	return PluginsDir() + name + u".new"_q;
}

QString ManifestPath(const QString &name) {
	return PluginDir(name) + u"/plugin.json"_q;
}

const std::vector<Installed> &List() {
	Load();
	return Data().list;
}

const Installed *Find(const QString &name) {
	auto &list = Data().list;
	const auto i = ranges::find(list, name, &Installed::name);
	return (i != end(list)) ? &*i : nullptr;
}

void ApplyPendingOperations() {
	Load();

	auto &list = Data().list;
	auto changed = false;
	for (auto i = begin(list); i != end(list);) {
		const auto folder = PluginDir(i->name);
		const auto updateFolder = PluginUpdateDir(i->name);
		if (i->deleted) {
			RemoveFolder(folder);
			RemoveFolder(updateFolder);
			LOG(("Plugins Info: Removed '%1'.").arg(i->name));
			i = list.erase(i);
			changed = true;
			continue;
		}
		if (i->pendingVersion) {
			// A loaded library cannot be replaced on Windows, so the
			// downloaded folder waited next to it until this point.
			if (QDir(updateFolder).exists()) {
				RemoveFolder(folder);
				if (QDir().rename(updateFolder, folder)) {
					LOG(("Plugins Info: Updated '%1' to version %2."
						).arg(i->name
						).arg(i->pendingVersion));
					i->version = i->pendingVersion;
				} else {
					LOG(("Plugins Error: Could not apply update of '%1'."
						).arg(i->name));
				}
			}
			i->pendingVersion = 0;
			changed = true;
		}
		if (!QFile::exists(ManifestPath(i->name))) {
			// Removed by hand outside the application.
			LOG(("Plugins Info: '%1' is gone, forgetting it.").arg(i->name));
			RemoveFolder(folder);
			i = list.erase(i);
			changed = true;
			continue;
		}
		++i;
	}

	// Folders nobody installed are never loaded: the libraries run
	// inside this process, so dropping one in must not be enough to get
	// it executed.
	const auto dir = QDir(PluginsDir());
	if (dir.exists()) {
		const auto entries = dir.entryInfoList(
			QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
		for (const auto &info : entries) {
			auto name = info.fileName();
			if (name.endsWith(u".new"_q)) {
				name.chop(4);
			}
			if (Find(name)) {
				continue;
			}
			LOG(("Plugins Info: Removing unknown entry '%1'."
				).arg(info.fileName()));
			if (info.isDir()) {
				RemoveFolder(info.absoluteFilePath());
			} else if (!QFile::remove(info.absoluteFilePath())) {
				LOG(("Plugins Error: Could not remove '%1'."
					).arg(info.absoluteFilePath()));
			}
		}
	}

	if (changed) {
		Save();
		Data().changes.fire({});
	}
}

void RegisterInstalled(const QString &name, int version) {
	Load();
	if (const auto entry = Lookup(name)) {
		entry->version = version;
		entry->deleted = false;
		entry->pendingVersion = 0;
	} else {
		Data().list.push_back({ .name = name, .version = version });
	}
	Save();
	Data().changes.fire({});
}

void RegisterPendingUpdate(const QString &name, int version) {
	Load();
	if (const auto entry = Lookup(name)) {
		entry->pendingVersion = version;
		entry->deleted = false;
		Save();
		Data().changes.fire({});
	}
}

void MarkDeleted(const QString &name, bool deleted) {
	Load();
	if (const auto entry = Lookup(name)) {
		entry->deleted = deleted;
		Save();
		Data().changes.fire({});
	}
}

rpl::producer<> Changes() {
	return Data().changes.events();
}

} // namespace Plugins
