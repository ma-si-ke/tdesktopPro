/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/required.h"

class PeerData;

namespace Data {

// Returns true when the peer should be hidden from user-facing surfaces
// (chat list, search, notifications, recipient pickers). The peer object,
// its messages, and internal uses (changelog, background preview, etc.)
// are not affected.
//
// Currently hardcoded: only PeerData::kServiceNotificationsId (777000).
[[nodiscard]] bool IsHiddenSystemUser(not_null<PeerData*> peer);

} // namespace Data
