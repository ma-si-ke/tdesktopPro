/*
This file is part of TelegramProDesktop, a customized fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "employee/employee_config.h"

#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>

#include <functional>
#include <memory>
#include <optional>
#include <variant>

class QNetworkAccessManager;
class QNetworkReply;

namespace Employee {

struct LoginRequest {
	QString employeeId;
	QString password;
	QString deviceId;
	QString deviceInfo;
	bool forceLogin = false;
	BackendType backend = kDefaultBackend;
};

struct TdesktopSession {
	int dcId = 0;
	QByteArray authKey; // 原始 256 字节，非 hex
	qint64 userId = 0;  // Telegram 用户 ID (GramJS StringSession.userId)
};

struct EmployeeInfo {
	QString id;
	QString employeeId;
	QString name;
	QString role;
};

struct LoginSuccess {
	QString token;
	QString deviceId;
	EmployeeInfo employee;
	TdesktopSession session;
	QJsonObject permissions;
};

enum class LoginError {
	Network,
	AlreadyOnline,
	NotBound,
	InvalidCredentials,
	InvalidResponse,
	Unknown,
};

struct LoginFailure {
	LoginError error = LoginError::Unknown;
	QString message;                 // 面向用户的中文提示
	QString conflictDeviceInfo;      // AlreadyOnline 专用
	QString conflictLastActiveAt;    // AlreadyOnline 专用
};

using LoginResult = std::variant<LoginSuccess, LoginFailure>;

class AuthClient final : public QObject {
public:
	explicit AuthClient(QObject *parent = nullptr);
	~AuthClient();

	using Callback = std::function<void(LoginResult)>;

	void submit(const LoginRequest &request, Callback done);
	void cancel();

private:
	void handleReply(QNetworkReply *reply, const Callback &done);
	[[nodiscard]] static std::optional<LoginSuccess> parseSuccess(
		const QJsonObject &obj,
		LoginFailure &outFailure);

	std::unique_ptr<QNetworkAccessManager> _manager;
	QPointer<QNetworkReply> _reply;

};

} // namespace Employee
