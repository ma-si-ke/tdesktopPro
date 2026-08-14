/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/service_config.h"

#include "core/application.h"
#include "core/core_settings.h"

namespace Core {
namespace {

// Kept from the days this was the shared notes address alone, so that
// an address configured before stays in effect.
constexpr auto kUrlPrefKey = "cloud_memo/base_url";

constexpr auto kCallAudioUrlPrefKey = "call_audio/ws_url";

} // namespace

QString DefaultServiceUrl() {
	return u"http://43.132.171.63:3100"_q;
}

QString ServiceUrl() {
	auto url = Core::App().settings().readPref<QString>(kUrlPrefKey);
	url = url.trimmed();
	if (!url.startsWith(u"http://"_q) && !url.startsWith(u"https://"_q)) {
		url = DefaultServiceUrl();
	}
	while (url.endsWith('/')) {
		url.chop(1);
	}
	return url;
}

void SetServiceUrl(const QString &url) {
	Core::App().settings().writePref<QString>(kUrlPrefKey, url.trimmed());
}

QString DefaultCallAudioUrl() {
	return u"wss://audioserver.kakaco.top/stream"_q;
}

QString CallAudioUrl() {
	auto url = Core::App().settings().readPref<QString>(kCallAudioUrlPrefKey);
	url = url.trimmed();
	if (!url.startsWith(u"ws://"_q) && !url.startsWith(u"wss://"_q)) {
		url = DefaultCallAudioUrl();
	}
	while (url.endsWith('/')) {
		url.chop(1);
	}
	return url;
}

void SetCallAudioUrl(const QString &url) {
	Core::App().settings().writePref<QString>(
		kCallAudioUrlPrefKey,
		url.trimmed());
}

QString CallAudioApiKey() {
	return u"fbb133b5b4ee5d284752e566da55c7894bfdca2fe7d4a644"_q;
}

} // namespace Core
