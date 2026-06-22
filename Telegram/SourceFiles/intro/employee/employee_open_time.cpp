/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_open_time.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <QtCore/QStringList>

#include <optional>

namespace Intro::Employee {
namespace {

[[nodiscard]] std::optional<int> ParseMinutes(const QString &hhmm) {
	const auto parts = hhmm.trimmed().split(':');
	if (parts.size() != 2) {
		return std::nullopt;
	}
	auto okHour = false;
	auto okMinute = false;
	const auto hour = parts[0].toInt(&okHour);
	const auto minute = parts[1].toInt(&okMinute);
	if (!okHour || !okMinute
		|| hour < 0 || hour > 23
		|| minute < 0 || minute > 59) {
		return std::nullopt;
	}
	return hour * 60 + minute;
}

} // namespace

bool IsWithinOpenTime(const QString &openTime, QTime now) {
	const auto trimmed = openTime.trimmed();
	if (trimmed.isEmpty()) {
		return true;
	}
	const auto dash = trimmed.indexOf('-');
	if (dash < 0) {
		return true;
	}
	const auto start = ParseMinutes(trimmed.left(dash));
	const auto end = ParseMinutes(trimmed.mid(dash + 1));
	if (!start || !end) {
		return true;
	} else if (*start == *end) {
		return true;
	}
	const auto current = now.hour() * 60 + now.minute();
	if (*start < *end) {
		return (current >= *start) && (current <= *end);
	}
	return (current >= *start) || (current <= *end);
}

bool IsWithinOpenTimeNow(const QString &openTime) {
	return IsWithinOpenTime(openTime, QTime::currentTime());
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
