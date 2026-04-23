/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_permissions.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/debug_log.h"

namespace Intro::Employee {

Permissions::Permissions() : _values(PermissionValues{}) {
}

Permissions::~Permissions() = default;

bool Permissions::has(PermissionKey key) const {
	const auto index = static_cast<int>(key);
	Expects(index >= 0 && index < kPermissionCount);
	return _values.current()[index];
}

rpl::producer<bool> Permissions::value(PermissionKey key) const {
	const auto index = static_cast<int>(key);
	Expects(index >= 0 && index < kPermissionCount);
	return _values.value() | rpl::map([=](const PermissionValues &v) {
		return v[index];
	}) | rpl::distinct_until_changed();
}

rpl::producer<> Permissions::changes() const {
	return _values.changes() | rpl::to_empty;
}

const QString &Permissions::token() const {
	return _token;
}

bool Permissions::authorized() const {
	return !_token.isEmpty();
}

void Permissions::apply(PermissionValues values, QString token) {
	LOG(("Employee: Permissions::apply tokenLen=%1 edit=%2 forward=%3 delete=%4 send=%5"
		).arg(token.size()
		).arg(values[int(PermissionKey::MsgEdit)]
		).arg(values[int(PermissionKey::MsgForward)]
		).arg(values[int(PermissionKey::MsgDelete)]
		).arg(values[int(PermissionKey::MsgSend)]));
	_token = std::move(token);
	_values = values;
}

void Permissions::clear() {
	_token = QString();
	_values = PermissionValues{};
}

const base::flat_map<QString, PermissionKey> &JsonKeyToPermission() {
	static const auto kMap = base::flat_map<QString, PermissionKey>{
		{ u"msg.send"_q,                   PermissionKey::MsgSend },
		{ u"msg.edit"_q,                   PermissionKey::MsgEdit },
		{ u"msg.delete"_q,                 PermissionKey::MsgDelete },
		{ u"msg.forward"_q,                PermissionKey::MsgForward },
		{ u"group.create"_q,               PermissionKey::GroupCreate },
		{ u"group.delete"_q,               PermissionKey::GroupDelete },
		{ u"group.addMember"_q,            PermissionKey::GroupAddMember },
		{ u"group.removeMember"_q,         PermissionKey::GroupRemoveMember },
		{ u"contact.add"_q,                PermissionKey::ContactAdd },
		{ u"contact.block"_q,              PermissionKey::ContactBlock },
		{ u"contact.editNote"_q,           PermissionKey::ContactEditNote },
		{ u"folder.edit"_q,                PermissionKey::FolderEdit },
		{ u"folder.addChat"_q,             PermissionKey::FolderAddChat },
		{ u"ui.disableMentionTooltip"_q,   PermissionKey::UiDisableMentionTooltip },
	};
	return kMap;
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
