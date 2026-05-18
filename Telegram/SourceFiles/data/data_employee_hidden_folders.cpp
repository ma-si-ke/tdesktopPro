/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_employee_hidden_folders.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "intro/employee/employee_hidden_folders.h"
#include "main/main_account.h"
#include "main/main_session.h"

namespace Data {

EmployeeHiddenFolders::EmployeeHiddenFolders(not_null<Session*> owner)
: _owner(owner) {
	// Filter membership changed (user added/removed a chat to/from any
	// folder, or a folder was renamed). Force every history to re-run
	// shouldBeInChatList(), which calls back into IsHiddenSystemUser and
	// flips chats in/out of the visible list.
	_owner->chatsFilters().changed(
	) | rpl::on_next([this] {
		refreshAllHistories();
	}, _lifetime);

	// Server-pushed name list changed. Same fan-out: every history must
	// re-check its visibility against the new names.
	_owner->session().account().employeeHiddenFolders().changes(
	) | rpl::on_next([this] {
		refreshAllHistories();
	}, _lifetime);
}

EmployeeHiddenFolders::~EmployeeHiddenFolders() = default;

bool EmployeeHiddenFolders::contains(not_null<PeerData*> peer) const {
	// Whitelist semantics: the server-pushed name list enumerates the
	// folders the employee IS allowed to see; everything else is hidden.
	// Field name "hiddenFolderNames" is preserved on the wire for backend
	// compatibility — it's effectively visibleFolderNames now.
	//
	// Three states:
	//   []        => fail-open, show everything (policy not configured)
	//   ["1"]     => hide-all sentinel, isolate the employee completely
	//   [...]     => standard whitelist; "1" inside a longer list is a
	//                literal folder name with no special meaning
	const auto &cfg = _owner->session().account().employeeHiddenFolders();
	const auto &names = cfg.names();
	if (names.isEmpty()) {
		return false;
	}
	if (cfg.isHideAllSentinel()) {
		// Strict isolation: hide every peer regardless of history /
		// folder membership. Even fresh search results disappear.
		return true;
	}
	const auto history = _owner->historyLoaded(peer);
	if (!history) {
		// No history => peer has never appeared in any chat list. It can't
		// belong to a whitelisted folder by membership rules, but hiding
		// fresh search-result peers would break first-contact flows
		// (e.g. searching a new username). Keep these visible.
		return false;
	}
	for (const auto &filter : _owner->chatsFilters().list()) {
		if (!names.contains(filter.title().text.text)) {
			continue;
		}
		if (filter.contains(history)) {
			// In at least one whitelisted folder => visible.
			return false;
		}
	}
	// History exists but is in no whitelisted folder => hidden.
	return true;
}

void EmployeeHiddenFolders::refreshAllHistories() {
	const auto refresh = [&](not_null<PeerData*> peer) {
		if (const auto history = _owner->historyLoaded(peer)) {
			history->updateChatListExistence();
		}
	};
	_owner->enumerateUsers([&](not_null<UserData*> user) {
		refresh(user);
	});
	_owner->enumerateGroups([&](not_null<PeerData*> peer) {
		refresh(peer);
	});
	_owner->enumerateBroadcasts([&](not_null<ChannelData*> channel) {
		refresh(channel);
	});
}

} // namespace Data

#endif // TDESKTOP_EMPLOYEE_MODE
