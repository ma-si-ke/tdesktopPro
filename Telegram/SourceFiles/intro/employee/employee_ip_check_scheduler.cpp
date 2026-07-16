/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_ip_check_scheduler.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_ip_check_box.h"
#include "base/debug_log.h"

namespace Intro::Employee {
namespace {

constexpr auto kStartupDelayMs = crl::time(2 * 1000);
constexpr auto kPeriodMs = 60 * crl::time(60 * 1000);

} // namespace

IpCheckScheduler::IpCheckScheduler()
: _startupTimer([=] { fire(true); })
, _periodicTimer([=] { onPeriodicTick(); }) {
}

void IpCheckScheduler::startupCheck() {
	_startupTimer.callOnce(kStartupDelayMs);
}

void IpCheckScheduler::startPeriodic() {
	_periodicTimer.callEach(kPeriodMs);
}

void IpCheckScheduler::stop() {
	_startupTimer.cancel();
	_periodicTimer.cancel();
	_client.cancel();
	_checking = false;
}

void IpCheckScheduler::onPeriodicTick() {
	if (!PeriodicIpCheckEnabled()) {
		LOG(("Employee: ipcheck periodic tick skipped (disabled)"));
		return;
	}
	fire(false);
}

void IpCheckScheduler::fire(bool startup) {
	if (_checking) {
		return;
	}
	_checking = true;
	LOG(("Employee: ipcheck background check startup=%1").arg(startup));
	_client.check(nullptr, [=](IpCheckResult result) {
		applyResult(startup, std::move(result));
	});
}

void IpCheckScheduler::applyResult(bool startup, IpCheckResult result) {
	_checking = false;
	if (const auto failure = std::get_if<IpCheckFailure>(&result)) {
		LOG(("Employee: ipcheck background check failed kind=%1"
			).arg(int(failure->kind)));
		return;
	}
	const auto &info = std::get<IpCheckInfo>(result);
	const auto risky = IsRiskyIp(info);
	const auto show = risky || (startup && DebugShowIpPanelEnabled());
	LOG(("Employee: ipcheck background result risky=%1 show=%2"
		).arg(risky
		).arg(show));
	if (!show || _box) {
		return;
	}
	_box = ShowIpCheckResultBox(info);
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
