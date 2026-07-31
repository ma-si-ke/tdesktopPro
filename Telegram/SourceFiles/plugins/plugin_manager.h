/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Plugins {

class Instance;

// Applies the deferred install, update and remove operations, then
// loads every installed plugin. Called once at startup, before
// anything may use a plugin.
void Start();

// Loaded plugins, in manifest name order. Instances live as long as
// the process does.
[[nodiscard]] const std::vector<not_null<Instance*>> &Loaded();

[[nodiscard]] Instance *FindLoaded(const QString &name);

} // namespace Plugins
