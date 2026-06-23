/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_permissions.h"

class QAction;
class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Intro::Employee {

// One-shot: read the current permission value and set enabled state on
// a QAction. Use for short-lived widgets (menu items, transient Box buttons)
// that are rebuilt on each user interaction.
void GuardAction(
	not_null<Main::Session*> session,
	PermissionKey key,
	not_null<QAction*> action);

// Reactive: subscribe the widget's enabled state to permission changes.
// False -> setDisabled(true) + WA_TransparentForMouseEvents=true.
// Subscription attached to widget->lifetime(); auto-released on destruction.
// Use for long-lived widgets (compose controls, info-page action buttons).
void BindDisabled(
	not_null<Main::Session*> session,
	PermissionKey key,
	not_null<Ui::RpWidget*> widget);

// Pure read. Use in callback early-return for non-button entry points:
// Enter key, Delete/Backspace, drag-drop, paste, keyboard shortcuts.
[[nodiscard]] bool Allowed(
	not_null<Main::Session*> session,
	PermissionKey key);

// The always-reachable emergency contact (developer). Sending to this peer is
// never blocked by the MsgSend permission, and its dedicated window is exempt
// from the open-time gate.
[[nodiscard]] bool IsEmergencyContact(PeerData *peer);

// MsgSend permission OR the emergency contact. Use at message-send entry
// points instead of Allowed(..., MsgSend).
[[nodiscard]] bool SendAllowed(
	not_null<Main::Session*> session,
	PeerData *peer);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
