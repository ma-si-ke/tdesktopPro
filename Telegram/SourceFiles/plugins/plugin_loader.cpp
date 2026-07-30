/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "plugins/plugin_loader.h"

#include "base/debug_log.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFileInfo>

namespace Plugins {

Loader::Loader(const QString &name) {
#ifdef Q_OS_WIN
	// Absolute path only, the system library search path must never be
	// used here - the process may be running elevated. The plugins
	// folder next to the binary may be missing entirely, that simply
	// means no plugins are installed.
	const auto path = QCoreApplication::applicationDirPath()
		+ u"/plugins/"_q
		+ name
		+ u".dll"_q;
	if (!QFileInfo::exists(path)) {
		_error = u"not found"_q;
		return;
	}
	_library.setFileName(path);
	if (!_library.load()) {
		_error = _library.errorString();
		LOG(("Plugins: Failed to load %1: %2").arg(path, _error));
		return;
	}
	_loaded = true;
	LOG(("Plugins: Loaded %1.").arg(path));
#else // Q_OS_WIN
	_error = u"plugins are Windows-only"_q;
#endif // Q_OS_WIN
}

QFunctionPointer Loader::resolveRaw(const char *symbol) {
	if (!_loaded) {
		return nullptr;
	}
	const auto result = _library.resolve(symbol);
	if (!result) {
		_loaded = false;
		_error = u"missing symbol "_q + symbol;
		LOG(("Plugins: %1 in %2.").arg(_error, _library.fileName()));
	}
	return result;
}

} // namespace Plugins
