/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "data/data_types.h"

namespace Main {
class Session;
} // namespace Main

namespace Intro::Employee {

// Called on chat switch. GET http://<host>/api/v1/staff/{peerTgId} returns
// { "tgId", "entries": [ { "userId", "type", "messageIds": [...] } ] }.
// Entries authored by this account (userId == self) or by a same-type account
// (type == this account's type) are dropped; the union of the rest is stored
// as the peer's masked-message set in Data::Session (permanent spoiler + no
// context menu). Same host as CloudMemo / the encrypt endpoint.
void FetchMaskedMessages(not_null<Main::Session*> session, PeerId peerId);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
