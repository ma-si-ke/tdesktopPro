/*
This file is part of TelegramProDesktop, a customized fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "employee/employee_session_inject.h"
#include "employee/employee_storage.h"

#include "data/data_peer_id.h"
#include "main/main_account.h"
#include "mtproto/mtproto_auth_key.h"

#include <cstring>

namespace Employee {

bool InjectSessionInto(
		not_null<Main::Account*> account,
		const StoredSession &stored) {
	if (stored.session.authKey.size() != 256) {
		return false;
	}
	if (stored.session.dcId < 1 || stored.session.dcId > 5) {
		return false;
	}
	if (stored.session.userId <= 0) {
		return false;
	}

	auto keyData = MTP::AuthKey::Data();
	static_assert(
		sizeof(MTP::AuthKey::Data) == 256,
		"MTP::AuthKey::Data expected to be 256 bytes");
	std::memcpy(
		keyData.data(),
		stored.session.authKey.constData(),
		256);

	auto authKey = std::make_shared<MTP::AuthKey>(
		MTP::AuthKey::Type::ReadFromFile,
		MTP::DcId(stored.session.dcId),
		keyData);

	// Account::start() runs very early in Application::startDomain(), so
	// by the time we can intervene the Account already has an empty MTP
	// instance. Use applyEmployeeBootstrap() to tear it down and rebuild
	// with our injected key.
	account->applyEmployeeBootstrap(
		MTP::DcId(stored.session.dcId),
		std::move(authKey),
		UserId(BareId(stored.session.userId)));

	return true;
}

} // namespace Employee
