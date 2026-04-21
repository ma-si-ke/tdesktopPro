/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_config.h"

namespace Intro::Employee::Prefs {

[[nodiscard]] BackendType LastBackend();
void SetLastBackend(BackendType backend);

[[nodiscard]] QString LastUsername();
void SetLastUsername(const QString &username);

} // namespace Intro::Employee::Prefs

#endif // TDESKTOP_EMPLOYEE_MODE
