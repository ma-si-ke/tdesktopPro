/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/timer.h"
#include "plugins/plugin_loader.h"
#include "plugins/plugin_manifest.h"

namespace Plugins {

// What a call gave back. Text values update the labels bound to them.
struct CallResult {
	QString toast;
	QString error;
	base::flat_map<QString, QString> texts;
	bool refresh = false;
};

class Instance final {
public:
	Instance(Manifest manifest, const QString &folder);
	~Instance();

	[[nodiscard]] const Manifest &manifest() const {
		return _manifest;
	}

	// False when the library is missing, broken or refused to start.
	[[nodiscard]] bool available() const {
		return _available;
	}
	[[nodiscard]] QString unavailableReason() const {
		return _reason;
	}

	// Zero unless a cooldown of this action is still running.
	[[nodiscard]] int cooldownLeft(const QString &action) const;
	[[nodiscard]] bool running() const {
		return _running;
	}

	// Runs `action`; `cooldown` and `async` come from the row or tray
	// item that triggered it. The callback is always on the main thread
	// and is skipped when the call was rejected outright.
	void call(
		const QString &action,
		bool async,
		int cooldown,
		Fn<void(CallResult)> done);

	void startTicks();
	void stopTicks();

	[[nodiscard]] rpl::producer<CallResult> tickResults() const;

private:
	[[nodiscard]] CallResult invoke(
		const QString &action,
		const QByteArray &args);
	void tick();

	Manifest _manifest;
	Loader _loader;
	bool _available = false;
	bool _running = false;
	QString _reason;

	base::flat_map<QString, crl::time> _cooldowns;
	base::Timer _tickTimer;
	bool _ticking = false;
	rpl::event_stream<CallResult> _tickResults;

	void *_call = nullptr;
	void *_shutdown = nullptr;

};

} // namespace Plugins
