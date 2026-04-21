/*
This file is part of TelegramProDesktop, a customized fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QString>
#include <QtCore/QVector>

namespace Employee {

enum class BackendType {
	CustomerService, // 客服
	Staff,           // 员工号
	Admin,           // 后台
	Other,           // 其他
};

struct BackendOption {
	BackendType type = BackendType::CustomerService;
	QString label;
	QString host;
	int port = 3000;
};

[[nodiscard]] const QVector<BackendOption> &BackendOptions();
[[nodiscard]] const BackendOption &BackendByType(BackendType type);
[[nodiscard]] QString BuildLoginUrl(const BackendOption &option);

inline constexpr auto kDefaultBackend = BackendType::CustomerService;

} // namespace Employee
