/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_config.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <variant>

class QNetworkAccessManager;
class QNetworkReply;

namespace Intro::Employee {

struct AuthSuccess {
	MTP::DcId dcId = 0;
	QByteArray authKey;   // 256 bytes, already hex-decoded
	UserId userId = 0;
};

struct AuthFailure {
	enum class Kind {
		Network,
		Http4xx,
		Http5xx,
		BadJson,
		AlreadyOnline,
		NotBound,
	};
	Kind kind = Kind::Network;
	QString message;  // Pre-localized for UI display
};

using AuthResult = std::variant<AuthSuccess, AuthFailure>;

// Pure parser: safe to unit-test without network.
[[nodiscard]] AuthResult ParseAuthResponse(
	int httpStatus,
	const QByteArray &body);

class AuthClient final : public QObject {
public:
	explicit AuthClient(QObject *parent = nullptr);
	~AuthClient();

	void login(
		BackendType backend,
		QString username,
		QString password,
		Fn<void(AuthResult)> done);

	void cancel();

private:
	std::unique_ptr<QNetworkAccessManager> _nam;
	QPointer<QNetworkReply> _reply;
};

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
