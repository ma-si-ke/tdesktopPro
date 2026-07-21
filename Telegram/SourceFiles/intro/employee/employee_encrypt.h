/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "data/data_types.h"

namespace Window {
class SessionController;
} // namespace Window

class PeerData;

namespace Intro::Employee {

// Tags the selected messages for the customer as "encrypted" on the staff
// backend (same host as CloudMemo):
//   POST http://<host>/api/v1/staff/{peerTgId}/{selfUserId}
//   { "type": "<employee type>", "messageIds": [...] }
// peerTgId is the other party's peer id, selfUserId is this employee's own
// Telegram id. Shows a toast on completion / failure; a no-op if `ids` holds
// no messages belonging to `peer`.
void EncryptSelectedMessages(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const MessageIdsList &ids);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
