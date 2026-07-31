/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "plugins/plugin_settings.h"

#include "base/debug_log.h"
#include "plugins/plugin_manifest.h"

#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSaveFile>

namespace Plugins::Settings {
namespace {

struct State {
	QJsonObject root;
	bool loaded = false;
};

[[nodiscard]] State &Data() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString Path() {
	return cWorkingDir() + u"tdata/plugin-settings.json"_q;
}

void Load() {
	auto &data = Data();
	if (data.loaded) {
		return;
	}
	data.loaded = true;
	auto file = QFile(Path());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto document = QJsonDocument::fromJson(file.readAll());
	if (document.isObject()) {
		data.root = document.object();
	} else {
		LOG(("Plugins Error: Bad settings file, starting empty."));
	}
}

void Save() {
	const auto content = QJsonDocument(
		Data().root
	).toJson(QJsonDocument::Indented);
	const auto path = Path();
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

[[nodiscard]] QJsonValue ReadValue(
		const Manifest &manifest,
		const QString &id) {
	Load();
	const auto plugin = Data().root.value(manifest.name);
	return plugin.isObject() ? plugin.toObject().value(id) : QJsonValue();
}

void WriteValue(
		const Manifest &manifest,
		const QString &id,
		QJsonValue value) {
	Load();
	auto &root = Data().root;
	auto plugin = root.value(manifest.name).toObject();
	plugin.insert(id, std::move(value));
	root.insert(manifest.name, plugin);
	Save();
}

} // namespace

bool ReadBool(const Manifest &manifest, const QString &id) {
	const auto definition = FindSetting(manifest, id);
	if (!definition || definition->type != SettingType::Bool) {
		return false;
	}
	const auto value = ReadValue(manifest, id);
	return value.isBool() ? value.toBool() : definition->boolDefault;
}

int ReadInt(const Manifest &manifest, const QString &id) {
	const auto definition = FindSetting(manifest, id);
	if (!definition || definition->type != SettingType::Int) {
		return 0;
	}
	const auto value = ReadValue(manifest, id);
	return value.isDouble()
		? std::clamp(int(value.toDouble()), definition->min, definition->max)
		: definition->intDefault;
}

QString ReadText(const Manifest &manifest, const QString &id) {
	const auto definition = FindSetting(manifest, id);
	if (!definition || definition->type != SettingType::Text) {
		return QString();
	}
	const auto value = ReadValue(manifest, id);
	return value.isString() ? value.toString() : definition->textDefault;
}

void WriteBool(const Manifest &manifest, const QString &id, bool value) {
	const auto definition = FindSetting(manifest, id);
	if (definition && definition->type == SettingType::Bool) {
		WriteValue(manifest, id, value);
	}
}

void WriteInt(const Manifest &manifest, const QString &id, int value) {
	const auto definition = FindSetting(manifest, id);
	if (definition && definition->type == SettingType::Int) {
		WriteValue(
			manifest,
			id,
			std::clamp(value, definition->min, definition->max));
	}
}

void WriteText(
		const Manifest &manifest,
		const QString &id,
		const QString &value) {
	const auto definition = FindSetting(manifest, id);
	if (definition && definition->type == SettingType::Text) {
		WriteValue(manifest, id, value);
	}
}

QJsonObject Collect(const Manifest &manifest) {
	auto result = QJsonObject();
	for (const auto &definition : manifest.settings) {
		switch (definition.type) {
		case SettingType::Bool:
			result.insert(definition.id, ReadBool(manifest, definition.id));
			break;
		case SettingType::Int:
			result.insert(definition.id, ReadInt(manifest, definition.id));
			break;
		case SettingType::Text:
			result.insert(definition.id, ReadText(manifest, definition.id));
			break;
		}
	}
	return result;
}

} // namespace Plugins::Settings
