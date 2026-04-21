/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_prefs.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <QtCore/QSettings>

namespace Intro::Employee::Prefs {
namespace {

[[nodiscard]] QSettings Open() {
	return QSettings(
		cWorkingDir() + u"tdata/employee_prefs.ini"_q,
		QSettings::IniFormat);
}

} // namespace

BackendType LastBackend() {
	const auto raw = Open().value(u"backend"_q, 0).toInt();
	if (raw >= 0 && raw < 4) {
		return BackendType(raw);
	}
	return BackendType::Customer;
}

void SetLastBackend(BackendType backend) {
	auto s = Open();
	s.setValue(u"backend"_q, int(backend));
	s.sync();
}

QString LastUsername() {
	return Open().value(u"username"_q, QString()).toString();
}

void SetLastUsername(const QString &username) {
	auto s = Open();
	s.setValue(u"username"_q, username);
	s.sync();
}

} // namespace Intro::Employee::Prefs

#endif // TDESKTOP_EMPLOYEE_MODE
