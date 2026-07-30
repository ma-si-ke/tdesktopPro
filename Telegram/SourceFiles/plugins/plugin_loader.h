/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QLibrary>

namespace Plugins {

// Loads an optional feature library strictly from the application
// directory. A missing or broken library makes the plugin unavailable,
// it never breaks the application itself. Loading happens once in the
// constructor, dropping the library in later requires a restart.
class Loader final {
public:
	explicit Loader(const QString &name);

	[[nodiscard]] bool loaded() const {
		return _loaded;
	}
	[[nodiscard]] QString error() const {
		return _error;
	}

	// Returns nullptr and marks the whole plugin as not loaded if the
	// symbol is missing, so a partially matching library never runs.
	template <typename Function>
	[[nodiscard]] Function resolve(const char *symbol) {
		return reinterpret_cast<Function>(resolveRaw(symbol));
	}

private:
	[[nodiscard]] QFunctionPointer resolveRaw(const char *symbol);

	QLibrary _library;
	bool _loaded = false;
	QString _error;

};

} // namespace Plugins
