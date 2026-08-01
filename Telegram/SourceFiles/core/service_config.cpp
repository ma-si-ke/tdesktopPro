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

} // namespace Core
