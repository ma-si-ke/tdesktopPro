/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Plugins {

inline constexpr auto kManifestVersion = 1;

enum class SettingType {
	Bool,
	Int,
	Text,
};

struct SettingDefinition {
	QString id;
	SettingType type = SettingType::Bool;
	bool boolDefault = false;
	int intDefault = 0;
	int min = std::numeric_limits<int>::min();
	int max = std::numeric_limits<int>::max();
	QString textDefault;
};

enum class RowType {
	Button,
	Toggle,
	Radio,
	Number,
	Text,
	Label,
	Divider,
};

struct RadioOption {
	QString text;
	int value = 0;
};

// A row is shown only while every entry of `visibleWhen` matches the
// current value of that setting.
struct VisibleCondition {
	QString setting;
	bool boolValue = false;
	int intValue = 0;
	bool isBool = false;
};

struct Row {
	RowType type = RowType::Divider;
	QString text;
	QString action;
	QString setting;
	QString bind;
	QString style;
	bool async = false;
	int cooldown = 0;
	std::vector<RadioOption> options;
	std::vector<VisibleCondition> visibleWhen;
};

struct TrayItem {
	QString text;
	QString action;
	bool async = false;
	int cooldown = 0;
};

struct Manifest {
	QString name;
	QString title;
	int version = 0;
	QString library;
	QString description;
	std::vector<SettingDefinition> settings;
	std::vector<Row> rows;
	std::vector<TrayItem> tray;
	int tickSeconds = 0;
};

// A manifest that does not parse as a whole is rejected: a plugin with
// half of its interface missing would be worse than one absent.
[[nodiscard]] std::optional<Manifest> ParseManifest(
	const QByteArray &json,
	const QString &expectedName);

[[nodiscard]] const SettingDefinition *FindSetting(
	const Manifest &manifest,
	const QString &id);

} // namespace Plugins
