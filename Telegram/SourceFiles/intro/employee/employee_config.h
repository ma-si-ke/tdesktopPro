/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <array>

namespace Intro::Employee {

enum class BackendType : uchar {
	Customer = 0,
	Staff    = 1,
	Admin    = 2,
	Other    = 3,
};

struct BackendInfo {
	QString label;
	QString host;
	int port = 0;
};

[[nodiscard]] const BackendInfo &BackendInfoFor(BackendType type);
[[nodiscard]] std::array<BackendInfo, 4> AllBackends();

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
