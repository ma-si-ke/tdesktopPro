/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_ip_check.h"
#include "base/timer.h"
#include "base/weak_qptr.h"

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Intro::Employee {

class IpCheckScheduler final {
public:
	IpCheckScheduler();

	void startupCheck();
	void startPeriodic();
	void stop();

private:
	void fire(bool startup);
	void onPeriodicTick();
	void applyResult(bool startup, IpCheckResult result);

	IpCheckClient _client;
	base::Timer _startupTimer;
	base::Timer _periodicTimer;
	base::weak_qptr<Ui::GenericBox> _box;
	bool _checking = false;

};

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
