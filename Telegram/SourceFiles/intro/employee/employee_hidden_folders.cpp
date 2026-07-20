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

HiddenFoldersMode HiddenFolders::mode() const {
	return _mode;
}

bool HiddenFolders::containsFolder(const QString &name) const {
	for (const auto &entry : _folders) {
		if (entry.name == name) {
			return true;
		}
	}
	return false;
}

bool HiddenFolders::isHideAll() const {
	return (_mode == HiddenFoldersMode::Whitelist) && _folders.empty();
}

rpl::producer<> HiddenFolders::changes() const {
	return _changes.events();
}

void HiddenFolders::apply(
		HiddenFoldersMode mode,
		std::vector<HiddenFolderEntry> folders) {
	LOG(("Employee: HiddenFolders::apply mode=%1 count=%2"
		).arg(int(mode)
		).arg(folders.size()));
	_mode = mode;
	_folders = std::move(folders);
	_changes.fire({});
}

void HiddenFolders::clear() {
	_mode = HiddenFoldersMode::Blacklist;
	_folders.clear();
	_changes.fire({});
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
