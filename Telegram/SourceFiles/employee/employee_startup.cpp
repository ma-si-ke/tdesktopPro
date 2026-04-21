/*
This file is part of TelegramProDesktop, a customized fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "employee/employee_startup.h"

#include "employee/employee_login_box.h"
#include "employee/employee_session_inject.h"
#include "employee/employee_storage.h"

#include "core/application.h"
#include "logs.h"
#include "main/main_account.h"
#include "ui/layers/box_content.h"
#include "ui/layers/generic_box.h"
#include "window/window_controller.h"

namespace Employee {
namespace {

void ShowLoginBoxAndInject(not_null<Main::Account*> account) {
	const auto window = Core::App().activeWindow();
	if (!window) {
		LOG(("Employee: no active window to show login box on."));
		return;
	}

	const auto onLoggedIn = [account](LoginSuccess login) {
		const auto stored = FromLoginSuccess(login);
		if (!WriteSession(stored)) {
			LOG(("Employee: failed to write employee.dat at %1"
				).arg(SessionFilePath()));
			return;
		}
		if (!InjectSessionInto(account, stored)) {
			LOG(("Employee: InjectSessionInto failed after login."));
			return;
		}
		LOG(("Employee: logged in and injected session for user %1"
			).arg(stored.session.userId));
	};

	window->show(Box(EmployeeLoginBox, onLoggedIn));
}

} // namespace

void MaybeInjectOrPromptLogin(not_null<Main::Account*> account) {
	// tdesktop 已经从 tdata 读取到 session，员工不需要再认证。
	if (account->sessionExists()) {
		return;
	}

	// 读已持久化的员工 session（跨重启复用）
	if (auto stored = ReadSession()) {
		if (InjectSessionInto(account, *stored)) {
			LOG(("Employee: injected session from employee.dat, user %1"
				).arg(stored->session.userId));
			return;
		}
		LOG(("Employee: employee.dat invalid, clearing and prompting."));
		ClearSession();
	}

	// 首次启动 / employee.dat 缺失：弹登录框
	ShowLoginBoxAndInject(account);
}

} // namespace Employee
