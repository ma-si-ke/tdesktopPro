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
	// Mode-based folder policy (priority currently ignored):
	//   Whitelist: hidden as soon as the peer appears in any folder OUTSIDE
	//              the list. Only-listed or in-no-folder peers stay visible.
	//              An empty whitelist hides everything (isHideAll()).
	//   Blacklist: hidden as soon as the peer appears in any listed folder.
	//              An empty blacklist lists nothing, so everyone shows.
	const auto &cfg = _owner->session().account().employeeHiddenFolders();
	const auto whitelist
		= (cfg.mode() == Intro::Employee::HiddenFoldersMode::Whitelist);

	if (cfg.isHideAll()) {
		// Empty whitelist: strict isolation, hide every peer regardless of
		// history or folder membership. Even fresh search results disappear.
		return true;
	}

	const auto history = _owner->historyLoaded(peer);
	if (!history) {
		// No history => peer has never appeared in any chat list, so it
		// belongs to no folder. Under both non-hide-all modes that means
		// visible; hiding fresh search-result peers would break first-contact
		// flows (e.g. searching a new username).
		return false;
	}

	auto inListed = false;
	auto inUnlisted = false;
	for (const auto &filter : _owner->chatsFilters().list()) {
		if (!filter.contains(history)) {
			continue;
		}
		if (cfg.containsFolder(filter.title().text.text)) {
			inListed = true;
		} else {
			inUnlisted = true;
		}
	}
	return whitelist ? inUnlisted : inListed;
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
