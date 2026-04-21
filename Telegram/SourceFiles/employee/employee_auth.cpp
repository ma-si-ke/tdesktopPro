/*
This file is part of TelegramProDesktop, a customized fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "employee/employee_auth.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Employee {
namespace {

constexpr auto kRequestTimeoutMs = 15000;

[[nodiscard]] QByteArray EncodeRequest(const LoginRequest &req) {
	auto obj = QJsonObject();
	obj.insert(u"employeeId"_q, req.employeeId);
	obj.insert(u"password"_q, req.password);
	obj.insert(u"deviceId"_q, req.deviceId);
	obj.insert(u"deviceInfo"_q, req.deviceInfo);
	obj.insert(u"forceLogin"_q, req.forceLogin);
	obj.insert(u"clientType"_q, u"tdesktop"_q);
	return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

} // namespace

AuthClient::AuthClient(QObject *parent)
: QObject(parent)
, _manager(std::make_unique<QNetworkAccessManager>(this)) {
}

AuthClient::~AuthClient() {
	cancel();
}

void AuthClient::submit(const LoginRequest &request, Callback done) {
	cancel();

	const auto &backend = BackendByType(request.backend);
	auto httpRequest = QNetworkRequest(QUrl(BuildLoginUrl(backend)));
	httpRequest.setHeader(
		QNetworkRequest::ContentTypeHeader,
		"application/json");
	httpRequest.setTransferTimeout(kRequestTimeoutMs);

	_reply = _manager->post(httpRequest, EncodeRequest(request));

	QObject::connect(
		_reply.data(),
		&QNetworkReply::finished,
		this,
		[this, done = std::move(done)]() {
			if (_reply) {
				handleReply(_reply, done);
			}
		});
}

void AuthClient::cancel() {
	if (_reply) {
		_reply->disconnect();
		_reply->abort();
		_reply->deleteLater();
		_reply = nullptr;
	}
}

void AuthClient::handleReply(QNetworkReply *reply, const Callback &done) {
	const auto body = reply->readAll();
	const auto networkError = reply->error();

	reply->deleteLater();
	_reply = nullptr;

	if (networkError == QNetworkReply::OperationCanceledError) {
		return; // 取消不回调
	}

	const auto json = QJsonDocument::fromJson(body);
	auto failure = LoginFailure();

	if (!json.isObject()) {
		failure.error = LoginError::Network;
		failure.message = u"网络错误，请检查连接"_q;
		done(failure);
		return;
	}

	const auto obj = json.object();
	const auto code = obj.value(u"code"_q).toString();

	if (code == u"ALREADY_ONLINE"_q) {
		failure.error = LoginError::AlreadyOnline;
		failure.message = u"账号已在其他设备登录"_q;
		failure.conflictDeviceInfo = obj.value(u"deviceInfo"_q).toString();
		failure.conflictLastActiveAt = obj.value(u"lastActiveAt"_q).toString();
		done(failure);
		return;
	}
	if (code == u"NOT_BOUND"_q) {
		failure.error = LoginError::NotBound;
		failure.message = u"账号未绑定 Telegram，请联系管理员"_q;
		done(failure);
		return;
	}

	if (obj.value(u"success"_q).toBool(false)) {
		auto parseFailure = LoginFailure();
		if (auto parsed = parseSuccess(obj, parseFailure)) {
			done(std::move(*parsed));
		} else {
			done(parseFailure);
		}
		return;
	}

	const auto errorMsg = obj.value(u"error"_q).toString();
	failure.error = LoginError::Unknown;
	failure.message = !errorMsg.isEmpty() ? errorMsg : u"登录失败"_q;
	done(failure);
}

std::optional<LoginSuccess> AuthClient::parseSuccess(
		const QJsonObject &obj,
		LoginFailure &outFailure) {
	const auto fail = [&](const QString &msg) {
		outFailure = LoginFailure{ LoginError::InvalidResponse, msg };
		return std::nullopt;
	};

	auto result = LoginSuccess();
	result.token = obj.value(u"token"_q).toString();
	result.deviceId = obj.value(u"deviceId"_q).toString();
	if (result.token.isEmpty() || result.deviceId.isEmpty()) {
		return fail(u"响应缺少 token 或 deviceId"_q);
	}

	const auto empObj = obj.value(u"employee"_q).toObject();
	result.employee.id = empObj.value(u"id"_q).toString();
	result.employee.employeeId = empObj.value(u"employeeId"_q).toString();
	result.employee.name = empObj.value(u"name"_q).toString();
	result.employee.role = empObj.value(u"role"_q).toString();
	if (result.employee.id.isEmpty() || result.employee.name.isEmpty()) {
		return fail(u"响应缺少员工信息"_q);
	}

	const auto sessionObj = obj.value(u"tdesktopSession"_q).toObject();
	result.session.dcId = sessionObj.value(u"dcId"_q).toInt(0);
	if (result.session.dcId < 1 || result.session.dcId > 5) {
		return fail(u"响应的 dcId 无效"_q);
	}
	const auto hex = sessionObj.value(u"authKeyHex"_q).toString();
	if (hex.size() != 512) {
		return fail(u"响应的 authKeyHex 长度不正确"_q);
	}
	result.session.authKey = QByteArray::fromHex(hex.toLatin1());
	if (result.session.authKey.size() != 256) {
		return fail(u"authKeyHex 不是合法十六进制"_q);
	}

	result.permissions = obj.value(u"permissions"_q).toObject();
	return result;
}

} // namespace Employee
