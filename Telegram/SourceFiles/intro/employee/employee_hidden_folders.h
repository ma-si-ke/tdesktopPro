/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <QtCore/QStringList>

#include <vector>

namespace Intro::Employee {

enum class HiddenFoldersMode : uchar {
	Blacklist = 0,
	Whitelist = 1,
};

struct HiddenFolderEntry {
	QString name;
	int priority = 0; // Range -9999..9999; currently ignored by consumers.
};

// Server-controlled folder-visibility policy for the employee. Fetched from
// the backend after login (HiddenFoldersClient) and refreshed on the verify
// cadence. NOT restored from disk: on cold start it stays at the default
// (Blacklist + empty = fail-open, show all) until the first fetch lands.
//
// Mode semantics (priority is parsed and stored but not yet used):
//   Whitelist: a peer is hidden as soon as it appears in ANY folder NOT in
//              the list; peers only in listed folders, or in no folder, show.
//              An EMPTY whitelist hides everything (isHideAll()).
//   Blacklist: a peer is hidden as soon as it appears in ANY listed folder;
//              an empty blacklist shows everything.
//
// UI surfaces consume mode() / containsFolder() to filter folder tabs;
// Data::Session's EmployeeHiddenFolders computes the per-peer visibility.
class HiddenFolders final {
public:
	HiddenFolders();
	~HiddenFolders();

	[[nodiscard]] HiddenFoldersMode mode() const;
	[[nodiscard]] bool containsFolder(const QString &name) const;

	// True only for an empty whitelist: hide every chat / folder regardless
	// of membership. Any blacklist, or a non-empty whitelist, is false.
	[[nodiscard]] bool isHideAll() const;

	[[nodiscard]] rpl::producer<> changes() const;

	void apply(HiddenFoldersMode mode, std::vector<HiddenFolderEntry> folders);
	void clear();

private:
	HiddenFoldersMode _mode = HiddenFoldersMode::Blacklist;
	std::vector<HiddenFolderEntry> _folders;
	rpl::event_stream<> _changes;
};

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
