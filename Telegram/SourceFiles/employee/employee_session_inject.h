/*
This file is part of TelegramProDesktop, a customized fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/basic_types.h"

namespace Main {
class Account;
} // namespace Main

namespace Employee {

struct StoredSession;

// 把 stored.session 注入到 Main::Account。
// 必须在 Account::start(config) 之前调用——所有底层 setter 都 Expects(!_mtp)。
// 返回 true 表示注入成功。
bool InjectSessionInto(
	not_null<Main::Account*> account,
	const StoredSession &stored);

} // namespace Employee
