/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <QtCore/QStringList>

namespace Intro::Employee {

// Server-controlled list of chat-filter (folder) titles that should be
// hidden from the employee. Loaded at cold start from the local snapshot,
// then refreshed once from the backend via HiddenFoldersClient. Strict
// case-sensitive equality match against ChatFilter::title().text.
//
// Lifetime: owned by Main::Account, mirrored across verify cycles. UI
// surfaces consume names() / value() to filter folder tabs; Data::Session
// computes a derived chat-id set for IsHiddenSystemUser.
class HiddenFolders final {
public:
	HiddenFolders();
	~HiddenFolders();

	[[nodiscard]] bool has(const QString &name) const;
	[[nodiscard]] const QStringList &names() const;
	[[nodiscard]] rpl::producer<QStringList> value() const;
	[[nodiscard]] rpl::producer<> changes() const;

	void apply(QStringList names);
	void clear();

private:
	rpl::variable<QStringList> _names;
};

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
