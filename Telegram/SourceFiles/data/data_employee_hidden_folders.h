/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

class PeerData;

namespace Data {

class Session;

// Lazy membership check: a peer is hidden when its history matches any
// ChatFilter whose title is in the server-supplied HiddenFolders name list.
// Uses ChatFilter::contains() so rule-based filters ("all groups", etc.)
// are honored, not just explicit always() / pinned() entries.
//
// Consumed by IsHiddenSystemUser (data_hidden_peers.cpp) to extend the
// 777000-only hide rule with employee-folder membership. Owned by
// Data::Session so PeerData* can reach it via peer->owner().
//
// No caching: each contains() call re-evaluates against the current
// filter list. Subscribes to chatsFilters and hidden-names change events
// so that adding/removing a chat from a hidden folder, or admin updates
// to the names list, refresh every history's chat-list membership.
class EmployeeHiddenFolders final {
public:
	explicit EmployeeHiddenFolders(not_null<Session*> owner);
	~EmployeeHiddenFolders();

	[[nodiscard]] bool contains(not_null<PeerData*> peer) const;

private:
	void refreshAllHistories();

	const not_null<Session*> _owner;
	rpl::lifetime _lifetime;
};

} // namespace Data

#endif // TDESKTOP_EMPLOYEE_MODE
