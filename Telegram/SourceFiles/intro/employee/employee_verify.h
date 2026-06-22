/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_config.h"
#include "intro/employee/employee_permissions.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <variant>

class QNetworkAccessManager;
class QNetworkReply;

namespace Intro::Employee {

struct VerifySuccess {
	PermissionValues permissions{};
	QString openTime; // "HH:MM-HH:MM" availability window; empty = always.
};

struct VerifyFailure {
	enum class Kind { Network, Server, InvalidToken, BadJson };
	Kind kind = Kind::Network;
};

using VerifyResult = std::variant<VerifySuccess, VerifyFailure>;

class VerifyClient final : public QObject {
public:
	explicit VerifyClient(QObject *parent = nullptr);
	~VerifyClient();

	void verify(
		BackendType backend,
		QString token,
		Fn<void(VerifyResult)> done);
	void cancel();

private:
	std::unique_ptr<QNetworkAccessManager> _nam;
	QPointer<QNetworkReply> _reply;
};

[[nodiscard]] VerifyResult ParseVerifyResponse(
	int httpStatus,
	const QByteArray &body);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
