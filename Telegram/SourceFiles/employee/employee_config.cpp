/*
This file is part of TelegramProDesktop, a customized fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "employee/employee_config.h"

namespace Employee {

const QVector<BackendOption> &BackendOptions() {
	static const auto kOptions = QVector<BackendOption>{
		{ BackendType::CustomerService, u"客服"_q,   u"101.32.15.201"_q,  3000 },
		{ BackendType::Staff,           u"员工号"_q, u"43.154.241.172"_q, 3000 },
		{ BackendType::Admin,           u"后台"_q,   u"129.226.182.152"_q, 3000 },
		{ BackendType::Other,           u"其他"_q,   u"43.132.171.63"_q,  3000 },
	};
	return kOptions;
}

const BackendOption &BackendByType(BackendType type) {
	const auto &all = BackendOptions();
	for (const auto &option : all) {
		if (option.type == type) {
			return option;
		}
	}
	return all.first();
}

QString BuildLoginUrl(const BackendOption &option) {
	return u"http://%1:%2/api/auth/login"_q
		.arg(option.host)
		.arg(option.port);
}

} // namespace Employee
