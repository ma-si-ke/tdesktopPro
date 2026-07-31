/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Plugins {

struct Installed {
	QString name;
	int version = 0;
	bool deleted = false;
	int pendingVersion = 0;
};

// Only names that survive this may become a file path.
[[nodiscard]] bool GoodPluginName(const QString &name);

[[nodiscard]] QString PluginsDir();

// A plugin is a folder holding plugin.json and its library. An update
// waits in the ".new" folder next to it, a loaded library cannot be
// replaced while the application runs.
[[nodiscard]] QString PluginDir(const QString &name);
[[nodiscard]] QString PluginUpdateDir(const QString &name);
[[nodiscard]] QString ManifestPath(const QString &name);

[[nodiscard]] const std::vector<Installed> &List();
[[nodiscard]] const Installed *Find(const QString &name);

// Reconciles the records with what is really on disk and performs the
// deferred operations. Must run before any plugin gets loaded: folders
// are still unlocked then and unknown ones must never be loaded.
void ApplyPendingOperations();

void RegisterInstalled(const QString &name, int version);
void RegisterPendingUpdate(const QString &name, int version);
void MarkDeleted(const QString &name, bool deleted);

[[nodiscard]] rpl::producer<> Changes();

} // namespace Plugins
