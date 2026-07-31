/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "settings/settings_type.h"

namespace Ui {
class RpWidget;
class VerticalLayout;
} // namespace Ui

namespace Window {
class SessionController;
} // namespace Window

namespace Plugins {
class Instance;
} // namespace Plugins

namespace Settings {

// Builds the interface a plugin describes in its manifest. Everything
// here is driven by data, adding a plugin never means adding code.
void FillPluginContent(
	not_null<Ui::VerticalLayout*> container,
	not_null<Window::SessionController*> controller,
	not_null<Plugins::Instance*> instance);

// One section per plugin, kept so that the same plugin always resolves
// to the same section type.
[[nodiscard]] Type PluginPageId(const QString &name);

} // namespace Settings
