/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QJsonObject;

namespace Plugins {

struct Manifest;

// Plugin settings live here, not in the plugin: a library that keeps no
// state of its own cannot disagree with what the interface shows.
namespace Settings {

[[nodiscard]] bool ReadBool(
	const Manifest &manifest,
	const QString &id);
[[nodiscard]] int ReadInt(const Manifest &manifest, const QString &id);
[[nodiscard]] QString ReadText(
	const Manifest &manifest,
	const QString &id);

void WriteBool(const Manifest &manifest, const QString &id, bool value);
void WriteInt(const Manifest &manifest, const QString &id, int value);
void WriteText(
	const Manifest &manifest,
	const QString &id,
	const QString &value);

// Every declared setting, defaults included, as passed to the library.
[[nodiscard]] QJsonObject Collect(const Manifest &manifest);

} // namespace Settings
} // namespace Plugins
