/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_hidden_peers.h"

#include "data/data_peer.h"

#ifdef TDESKTOP_EMPLOYEE_MODE
#include "data/data_employee_hidden_folders.h"
#include "data/data_session.h"
#endif

namespace Data {

bool IsHiddenSystemUser(not_null<PeerData*> peer) {
	if (peer->id == PeerData::kServiceNotificationsId) {
		return true;
	}
#ifdef TDESKTOP_EMPLOYEE_MODE
	if (peer->owner().employeeHiddenFolders().contains(peer)) {
		return true;
	}
#endif
	return false;
}

} // namespace Data
