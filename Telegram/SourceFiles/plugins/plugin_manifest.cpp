/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "plugins/plugin_manifest.h"

#include "base/debug_log.h"
#include "plugins/plugin_registry.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Plugins {
namespace {

constexpr auto kMaxRows = 128;
constexpr auto kMaxSettings = 64;
constexpr auto kMaxOptions = 32;
constexpr auto kMaxTrayItems = 8;
constexpr auto kMaxTextLength = 512;
constexpr auto kMinTickSeconds = 1;
constexpr auto kMaxTickSeconds = 3600;
constexpr auto kMaxCooldown = 3600;

[[nodiscard]] bool GoodIdentifier(const QString &id) {
	if (id.isEmpty() || id.size() > 64) {
		return false;
	}
	for (const auto &ch : id) {
		const auto good = (ch >= 'a' && ch <= 'z')
			|| (ch >= 'A' && ch <= 'Z')
			|| (ch >= '0' && ch <= '9')
			|| (ch == '_');
		if (!good) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool GoodText(const QString &text) {
	return text.size() <= kMaxTextLength;
}

// The library name becomes a path inside the plugin folder, so it must
// stay a plain file name.
[[nodiscard]] bool GoodLibraryName(const QString &name) {
	if (name.isEmpty()
		|| name.size() > 64
		|| !name.endsWith(u".dll"_q)
		|| name.contains('/')
		|| name.contains('\\')
		|| name.contains(':')
		|| name.contains(u".."_q)) {
		return false;
	}
	return true;
}

[[nodiscard]] std::optional<SettingType> ParseSettingType(
		const QString &type) {
	if (type == u"bool"_q) {
		return SettingType::Bool;
	} else if (type == u"int"_q) {
		return SettingType::Int;
	} else if (type == u"text"_q) {
		return SettingType::Text;
	}
	return std::nullopt;
}

[[nodiscard]] std::optional<RowType> ParseRowType(const QString &type) {
	if (type == u"button"_q) {
		return RowType::Button;
	} else if (type == u"toggle"_q) {
		return RowType::Toggle;
	} else if (type == u"radio"_q) {
		return RowType::Radio;
	} else if (type == u"number"_q) {
		return RowType::Number;
	} else if (type == u"text"_q) {
		return RowType::Text;
	} else if (type == u"label"_q) {
		return RowType::Label;
	} else if (type == u"divider"_q) {
		return RowType::Divider;
	}
	return std::nullopt;
}

[[nodiscard]] bool ParseSettings(
		const QJsonArray &array,
		std::vector<SettingDefinition> &to) {
	if (array.size() > kMaxSettings) {
		return false;
	}
	for (const auto &value : array) {
		if (!value.isObject()) {
			return false;
		}
		const auto object = value.toObject();
		auto entry = SettingDefinition();
		entry.id = object.value(u"id"_q).toString();
		const auto type = ParseSettingType(object.value(u"type"_q).toString());
		if (!GoodIdentifier(entry.id) || !type) {
			return false;
		}
		for (const auto &already : to) {
			if (already.id == entry.id) {
				return false;
			}
		}
		entry.type = *type;
		const auto fallback = object.value(u"default"_q);
		switch (entry.type) {
		case SettingType::Bool:
			entry.boolDefault = fallback.toBool();
			break;
		case SettingType::Int:
			if (object.contains(u"min"_q)) {
				entry.min = int(object.value(u"min"_q).toDouble());
			}
			if (object.contains(u"max"_q)) {
				entry.max = int(object.value(u"max"_q).toDouble());
			}
			if (entry.min > entry.max) {
				return false;
			}
			entry.intDefault = std::clamp(
				int(fallback.toDouble()),
				entry.min,
				entry.max);
			break;
		case SettingType::Text:
			entry.textDefault = fallback.toString();
			if (!GoodText(entry.textDefault)) {
				return false;
			}
			break;
		}
		to.push_back(std::move(entry));
	}
	return true;
}

[[nodiscard]] bool ParseVisibleWhen(
		const QJsonObject &object,
		const std::vector<SettingDefinition> &settings,
		std::vector<VisibleCondition> &to) {
	for (auto i = object.begin(); i != object.end(); ++i) {
		auto condition = VisibleCondition();
		condition.setting = i.key();
		const auto definition = ranges::find(
			settings,
			condition.setting,
			&SettingDefinition::id);
		if (definition == end(settings)) {
			return false;
		}
		const auto value = i.value();
		if (value.isBool()) {
			condition.isBool = true;
			condition.boolValue = value.toBool();
		} else if (value.isDouble()) {
			condition.intValue = int(value.toDouble());
		} else {
			return false;
		}
		to.push_back(std::move(condition));
	}
	return true;
}

[[nodiscard]] bool ParseRows(
		const QJsonArray &array,
		const std::vector<SettingDefinition> &settings,
		std::vector<Row> &to) {
	if (array.size() > kMaxRows) {
		return false;
	}
	for (const auto &value : array) {
		if (!value.isObject()) {
			return false;
		}
		const auto object = value.toObject();
		auto row = Row();
		const auto type = ParseRowType(object.value(u"type"_q).toString());
		if (!type) {
			return false;
		}
		row.type = *type;
		row.text = object.value(u"text"_q).toString();
		row.action = object.value(u"action"_q).toString();
		row.setting = object.value(u"setting"_q).toString();
		row.bind = object.value(u"bind"_q).toString();
		row.style = object.value(u"style"_q).toString();
		row.async = object.value(u"async"_q).toBool();
		row.cooldown = std::clamp(
			int(object.value(u"cooldown"_q).toDouble()),
			0,
			kMaxCooldown);
		if (!GoodText(row.text)) {
			return false;
		} else if (!row.action.isEmpty() && !GoodIdentifier(row.action)) {
			return false;
		} else if (!row.bind.isEmpty() && !GoodIdentifier(row.bind)) {
			return false;
		}
		if (!row.setting.isEmpty()) {
			const auto definition = ranges::find(
				settings,
				row.setting,
				&SettingDefinition::id);
			if (definition == end(settings)) {
				return false;
			}
		}
		switch (row.type) {
		case RowType::Button:
			if (row.action.isEmpty() || row.text.isEmpty()) {
				return false;
			}
			break;
		case RowType::Toggle:
		case RowType::Number:
			if (row.setting.isEmpty()) {
				return false;
			}
			break;
		case RowType::Radio: {
			if (row.setting.isEmpty()) {
				return false;
			}
			const auto options = object.value(u"options"_q).toArray();
			if (options.isEmpty() || options.size() > kMaxOptions) {
				return false;
			}
			for (const auto &option : options) {
				if (!option.isObject()) {
					return false;
				}
				const auto fields = option.toObject();
				auto parsed = RadioOption();
				parsed.text = fields.value(u"text"_q).toString();
				parsed.value = int(fields.value(u"value"_q).toDouble());
				if (parsed.text.isEmpty() || !GoodText(parsed.text)) {
					return false;
				}
				row.options.push_back(std::move(parsed));
			}
		} break;
		case RowType::Text:
		case RowType::Label:
			if (row.text.isEmpty() && row.bind.isEmpty()) {
				return false;
			}
			break;
		case RowType::Divider:
			break;
		}
		const auto visible = object.value(u"visibleWhen"_q);
		if (visible.isObject()) {
			if (!ParseVisibleWhen(
				visible.toObject(),
				settings,
				row.visibleWhen)) {
				return false;
			}
		} else if (!visible.isUndefined() && !visible.isNull()) {
			return false;
		}
		to.push_back(std::move(row));
	}
	return true;
}

[[nodiscard]] bool ParseTray(
		const QJsonArray &array,
		std::vector<TrayItem> &to) {
	if (array.size() > kMaxTrayItems) {
		return false;
	}
	for (const auto &value : array) {
		if (!value.isObject()) {
			return false;
		}
		const auto object = value.toObject();
		auto item = TrayItem();
		item.text = object.value(u"text"_q).toString();
		item.action = object.value(u"action"_q).toString();
		item.async = object.value(u"async"_q).toBool();
		item.cooldown = std::clamp(
			int(object.value(u"cooldown"_q).toDouble()),
			0,
			kMaxCooldown);
		if (item.text.isEmpty()
			|| !GoodText(item.text)
			|| !GoodIdentifier(item.action)) {
			return false;
		}
		to.push_back(std::move(item));
	}
	return true;
}

} // namespace

std::optional<Manifest> ParseManifest(
		const QByteArray &json,
		const QString &expectedName) {
	const auto fail = [&](const QString &reason) {
		LOG(("Plugins Error: Manifest of '%1' rejected: %2."
			).arg(expectedName, reason));
		return std::nullopt;
	};
	const auto document = QJsonDocument::fromJson(json);
	if (!document.isObject()) {
		return fail(u"not an object"_q);
	}
	const auto object = document.object();
	if (int(object.value(u"manifest"_q).toDouble()) != kManifestVersion) {
		return fail(u"unsupported manifest version"_q);
	}
	auto result = Manifest();
	result.name = object.value(u"name"_q).toString();
	if (!GoodPluginName(result.name) || result.name != expectedName) {
		return fail(u"bad name"_q);
	}
	result.version = int(object.value(u"version"_q).toDouble());
	if (result.version <= 0) {
		return fail(u"bad version"_q);
	}
	result.library = object.value(u"library"_q).toString();
	if (!GoodLibraryName(result.library)) {
		return fail(u"bad library name"_q);
	}
	result.title = object.value(u"title"_q).toString();
	if (result.title.isEmpty()) {
		result.title = result.name;
	}
	result.description = object.value(u"description"_q).toString();
	if (!GoodText(result.title) || !GoodText(result.description)) {
		return fail(u"text too long"_q);
	}
	if (const auto settings = object.value(u"settings"_q); !settings.isUndefined()) {
		if (!settings.isArray()
			|| !ParseSettings(settings.toArray(), result.settings)) {
			return fail(u"bad settings"_q);
		}
	}
	if (const auto ui = object.value(u"ui"_q); !ui.isUndefined()) {
		if (!ui.isArray()
			|| !ParseRows(ui.toArray(), result.settings, result.rows)) {
			return fail(u"bad ui"_q);
		}
	}
	if (const auto tray = object.value(u"tray"_q); !tray.isUndefined()) {
		if (!tray.isArray() || !ParseTray(tray.toArray(), result.tray)) {
			return fail(u"bad tray"_q);
		}
	}
	if (const auto tick = object.value(u"tick"_q); tick.isObject()) {
		const auto seconds = int(
			tick.toObject().value(u"everySeconds"_q).toDouble());
		if (seconds < kMinTickSeconds || seconds > kMaxTickSeconds) {
			return fail(u"bad tick interval"_q);
		}
		result.tickSeconds = seconds;
	} else if (!tick.isUndefined() && !tick.isNull()) {
		return fail(u"bad tick"_q);
	}
	return result;
}

const SettingDefinition *FindSetting(
		const Manifest &manifest,
		const QString &id) {
	const auto i = ranges::find(
		manifest.settings,
		id,
		&SettingDefinition::id);
	return (i != end(manifest.settings)) ? &*i : nullptr;
}

} // namespace Plugins
