/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_hidden_folders.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/debug_log.h"

namespace Intro::Employee {

HiddenFolders::HiddenFolders() = default;

HiddenFolders::~HiddenFolders() = default;

bool HiddenFolders::has(const QString &name) const {
	return _names.current().contains(name);
}

const QStringList &HiddenFolders::names() const {
	return _names.current();
}

rpl::producer<QStringList> HiddenFolders::value() const {
	return _names.value();
}

rpl::producer<> HiddenFolders::changes() const {
	return _names.changes() | rpl::to_empty;
}

void HiddenFolders::apply(QStringList names) {
	LOG(("Employee: HiddenFolders::apply count=%1").arg(names.size()));
	_names = std::move(names);
}

void HiddenFolders::clear() {
	_names = QStringList();
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
