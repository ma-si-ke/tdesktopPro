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
	{ u"客服"_q,   u"kefu.td.anyue.co"_q,     0 },
	{ u"员工号"_q, u"yuangong.td.anyue.co"_q, 0 },
	{ u"后台"_q,   u"houtai.td.anyue.co"_q,   0 },
	{ u"其他"_q,   u"qita.td.anyue.co"_q,     0 },
} };

} // namespace

const BackendInfo &BackendInfoFor(BackendType type) {
	Expects(int(type) >= 0 && int(type) < int(kBackends.size()));

	return kBackends[int(type)];
}

const std::array<BackendInfo, 4> &AllBackends() {
	return kBackends;
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
