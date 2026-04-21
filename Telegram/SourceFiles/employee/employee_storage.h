/*
This file is part of TelegramProDesktop, a customized fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "employee/employee_auth.h"

#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <optional>

namespace Employee {

struct StoredSession {
	QString token;
	QString deviceId;
	EmployeeInfo employee;
	QJsonObject permissions;
	TdesktopSession session;
};

[[nodiscard]] StoredSession FromLoginSuccess(const LoginSuccess &login);

// 读取 tdata/employee.dat。文件不存在、格式无效、或 authKey 长度不对时返回 nullopt。
[[nodiscard]] std::optional<StoredSession> ReadSession();

// 覆盖写入 tdata/employee.dat。原子写入（tmp 文件 + rename）。
bool WriteSession(const StoredSession &session);

// 删除 tdata/employee.dat。
void ClearSession();

// 返回 tdata/employee.dat 的绝对路径（便于调试）。
[[nodiscard]] QString SessionFilePath();

// 是否当前处于员工模式（即 tdata/employee.dat 存在）。
// 注意：这个读文件，不要高频调用。启动时缓存结果更好。
[[nodiscard]] bool IsActive();

// 读一次 employee.dat 里的员工姓名，用于 Telegram 活动会话里的设备名。
// 无效时返回空串。
[[nodiscard]] QString CurrentEmployeeDeviceName();

} // namespace Employee
