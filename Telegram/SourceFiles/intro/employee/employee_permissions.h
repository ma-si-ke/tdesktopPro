/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/flat_map.h"

#include <array>

namespace Intro::Employee {

enum class PermissionKey : int {
	MsgSend = 0,
	MsgEdit,
	MsgDelete,
	MsgForward,
	GroupCreate,
	GroupDelete,
	GroupAddMember,
	GroupRemoveMember,
	ContactAdd,
	ContactBlock,
	ContactEditNote,
	FolderEdit,
	FolderAddChat,
	UiDisableMentionTooltip,
};
constexpr int kPermissionCount = 14;

using PermissionValues = std::array<bool, kPermissionCount>;

class Permissions final {
public:
	Permissions();
	~Permissions();

	[[nodiscard]] bool has(PermissionKey key) const;
	[[nodiscard]] rpl::producer<bool> value(PermissionKey key) const;
	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] const QString &token() const;
	[[nodiscard]] bool authorized() const;

	void apply(PermissionValues values, QString token);
	void clear();

private:
	rpl::variable<PermissionValues> _values;
	QString _token;
};

// Backend JSON key (e.g. "msg.send") -> PermissionKey.
// Lookup: JsonKeyToPermission().find(key); missing = unknown, dropped by parser.
[[nodiscard]] const base::flat_map<QString, PermissionKey>
	&JsonKeyToPermission();

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
