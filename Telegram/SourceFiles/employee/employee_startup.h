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

// 在 Application::startDomain() 之后调用。
// - 如果 account 已经有 session（tdesktop 原生 tdata 里已存在）：什么都不做。
// - 否则读 tdata/employee.dat：有且有效就注入 session。
// - 都没有：在当前窗口上弹 EmployeeLoginBox，登录成功后写 employee.dat 并注入。
void MaybeInjectOrPromptLogin(not_null<Main::Account*> account);

} // namespace Employee
