/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_config.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

namespace Intro::Employee {
namespace {

const std::array<BackendInfo, 4> kBackends = { {
	{ u"客服"_q,   u"101.32.15.201"_q,  3000 },
	{ u"员工号"_q, u"43.154.241.172"_q, 3000 },
	{ u"后台"_q,   u"129.226.182.152"_q, 3000 },
	{ u"其他"_q,   u"43.132.171.63"_q,  3000 },
} };

} // namespace

const BackendInfo &BackendInfoFor(BackendType type) {
	const auto index = static_cast<int>(type);
	Expects(index >= 0 && index < int(kBackends.size()));

	return kBackends[index];
}

std::array<BackendInfo, 4> AllBackends() {
	return kBackends;
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
