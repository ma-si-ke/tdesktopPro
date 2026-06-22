/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <QtCore/QString>
#include <QtCore/QTime>

namespace Intro::Employee {

// Parses an "HH:MM-HH:MM" open-time window and tells whether `now` is inside.
// Rules:
//   - empty / unparsable                  -> open (fail-open)
//   - start == end (e.g. "00:00-00:00")   -> open 24h
//   - start <  end (e.g. "09:00-18:00")   -> [start, end] inclusive
//   - start >  end (e.g. "23:00-01:00")   -> crosses midnight: [start, 24:00) U [00:00, end]
// Minute granularity, both ends inclusive.
[[nodiscard]] bool IsWithinOpenTime(const QString &openTime, QTime now);
[[nodiscard]] bool IsWithinOpenTimeNow(const QString &openTime);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
