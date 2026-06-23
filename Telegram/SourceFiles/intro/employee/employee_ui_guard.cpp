/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_ui_guard.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "main/main_account.h"
#include "main/main_session.h"
#include "data/data_peer.h"
#include "ui/rp_widget.h"

#include <QtGui/QAction>

namespace Intro::Employee {

void GuardAction(
		not_null<Main::Session*> session,
		PermissionKey key,
		not_null<QAction*> action) {
	action->setEnabled(session->account().employeePermissions().has(key));
}

void BindDisabled(
		not_null<Main::Session*> session,
		PermissionKey key,
		not_null<Ui::RpWidget*> widget) {
	session->account().employeePermissions().value(
		key
	) | rpl::on_next([raw = widget.get()](bool allowed) {
		raw->setDisabled(!allowed);
		raw->setAttribute(
			Qt::WA_TransparentForMouseEvents,
			!allowed);
	}, widget->lifetime());
}

bool Allowed(
		not_null<Main::Session*> session,
		PermissionKey key) {
	return session->account().employeePermissions().has(key);
}

bool IsEmergencyContact(PeerData *peer) {
	return peer
		&& !peer->username().compare(u"kaka_co"_q, Qt::CaseInsensitive);
}

bool SendAllowed(not_null<Main::Session*> session, PeerData *peer) {
	return Allowed(session, PermissionKey::MsgSend)
		|| IsEmergencyContact(peer);
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
