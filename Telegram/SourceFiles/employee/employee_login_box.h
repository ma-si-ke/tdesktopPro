/*
This file is part of TelegramProDesktop, a customized fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "employee/employee_auth.h"

#include "base/object_ptr.h"

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Employee {

// 员工登录对话框。
// onLoggedIn 在登录成功后被调用，对话框自动关闭。
// 对话框自行处理 AlreadyOnline 的确认弹窗和错误显示。
void EmployeeLoginBox(
	not_null<Ui::GenericBox*> box,
	Fn<void(LoginSuccess)> onLoggedIn);

} // namespace Employee
