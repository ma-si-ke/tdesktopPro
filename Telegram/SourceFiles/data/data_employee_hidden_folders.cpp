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
	const auto &names = _owner->session().account()
		.employeeHiddenFolders().names();
	if (names.isEmpty()) {
		return false;
	}
	const auto history = _owner->historyLoaded(peer);
	if (!history) {
		// No history => peer has never appeared in any chat list, so
		// it cannot match a folder by either explicit entry or rule.
		return false;
	}
	for (const auto &filter : _owner->chatsFilters().list()) {
		if (!names.contains(filter.title().text.text)) {
			continue;
		}
		if (filter.contains(history)) {
			return true;
		}
	}
	return false;
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
