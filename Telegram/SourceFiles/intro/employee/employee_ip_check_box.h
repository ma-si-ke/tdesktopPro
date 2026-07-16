/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_ip_check.h"
#include "base/weak_qptr.h"

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Intro::Employee {

void IpCheckResultBox(not_null<Ui::GenericBox*> box, const IpCheckInfo &info);
[[nodiscard]] base::weak_qptr<Ui::GenericBox> ShowIpCheckResultBox(
	const IpCheckInfo &info);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
