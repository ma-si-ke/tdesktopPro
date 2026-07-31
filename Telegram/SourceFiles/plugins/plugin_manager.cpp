/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "plugins/plugin_manager.h"

#include "base/debug_log.h"
#include "plugins/plugin_instance.h"
#include "plugins/plugin_manifest.h"
#include "plugins/plugin_registry.h"

#include <QtCore/QFile>

namespace Plugins {
namespace {

// Instances are never destroyed: calls run on worker threads and the
// libraries stay mapped for the lifetime of the process.
[[nodiscard]] std::vector<not_null<Instance*>> &Instances() {
	static auto result = std::vector<not_null<Instance*>>();
	return result;
}

bool Started = false;

} // namespace

void Start() {
	if (Started) {
		return;
	}
	Started = true;

	ApplyPendingOperations();

	for (const auto &entry : List()) {
		if (entry.deleted) {
			continue;
		}
		auto file = QFile(ManifestPath(entry.name));
		if (!file.open(QIODevice::ReadOnly)) {
			LOG(("Plugins Error: No manifest for '%1'.").arg(entry.name));
			continue;
		}
		auto manifest = ParseManifest(file.readAll(), entry.name);
		if (!manifest) {
			continue;
		}
		const auto instance = new Instance(
			std::move(*manifest),
			PluginDir(entry.name));
		Instances().push_back(instance);
		if (instance->available()) {
			instance->startTicks();
		} else {
			LOG(("Plugins Info: '%1' unavailable: %2."
				).arg(entry.name, instance->unavailableReason()));
		}
	}
}

const std::vector<not_null<Instance*>> &Loaded() {
	return Instances();
}

Instance *FindLoaded(const QString &name) {
	for (const auto &instance : Instances()) {
		if (instance->manifest().name == name) {
			return instance;
		}
	}
	return nullptr;
}

} // namespace Plugins
