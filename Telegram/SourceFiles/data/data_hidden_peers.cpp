/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_hidden_peers.h"

#include "data/data_peer.h"

namespace Data {

bool IsHiddenSystemUser(not_null<PeerData*> peer) {
	return peer->id == PeerData::kServiceNotificationsId;
}

} // namespace Data
