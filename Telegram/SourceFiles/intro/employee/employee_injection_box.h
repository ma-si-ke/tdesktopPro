/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

namespace Intro::Employee {

// Returns false if no active window was available to host the box, so the
// caller can retry later; true once the box is shown (or already shown).
bool ShowInjectionDetectedBox();

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
