/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_verify.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/debug_log.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Intro::Employee {

VerifyResult ParseVerifyResponse(int httpStatus, const QByteArray &body) {
	if (httpStatus == 401) {
		LOG(("Employee: verify 401 invalid token"));
		return VerifyFailure{ VerifyFailure::Kind::InvalidToken };
	}
	if (httpStatus >= 500) {
		LOG(("Employee: verify server error http=%1").arg(httpStatus));
		return VerifyFailure{ VerifyFailure::Kind::Server };
	}
	if (httpStatus >= 400) {
		LOG(("Employee: verify client error http=%1").arg(httpStatus));
		return VerifyFailure{ VerifyFailure::Kind::Server };
	}

	auto parseError = QJsonParseError{};
	const auto doc = QJsonDocument::fromJson(body, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		LOG(("Employee: verify bad json"));
		return VerifyFailure{ VerifyFailure::Kind::BadJson };
	}
	const auto root = doc.object();
	const auto permsValue = root.value(u"permissions"_q);
	if (!permsValue.isObject()) {
		LOG(("Employee: verify response has no permissions object"));
		return VerifyFailure{ VerifyFailure::Kind::BadJson };
	}

	auto permissions = PermissionValues{};
	const auto permsObj = permsValue.toObject();
	const auto &mapping = JsonKeyToPermission();
	for (auto it = permsObj.begin(); it != permsObj.end(); ++it) {
		const auto known = mapping.find(it.key());
		if (known == mapping.end()) {
			LOG(("Employee: verify unknown key: %1").arg(it.key()));
			continue;
		}
		const auto rawValue = it.value();
		if (rawValue.isBool()) {
			permissions[static_cast<int>(known->second)] = rawValue.toBool();
		}
	}
	return VerifySuccess{ permissions };
}

VerifyClient::VerifyClient(QObject *parent)
: QObject(parent)
, _nam(std::make_unique<QNetworkAccessManager>()) {
}

VerifyClient::~VerifyClient() {
	cancel();
}

void VerifyClient::verify(
		BackendType backend,
		QString token,
		Fn<void(VerifyResult)> done) {
	cancel();

	if (token.isEmpty()) {
		done(VerifyFailure{ VerifyFailure::Kind::InvalidToken });
		return;
	}

	const auto info = BackendInfoFor(backend);
	auto url = QUrl();
	url.setScheme(u"http"_q);
	url.setHost(info.host);
	url.setPort(info.port);
	url.setPath(u"/api/auth/verify"_q);

	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader(
		"Authorization",
		(u"Bearer "_q + token).toLatin1());

	_reply = _nam->get(request);
	const auto reply = _reply.data();
	const auto finishedCallback = [this, reply, done = std::move(done)]() {
		if (!reply || reply != _reply.data()) {
			return;
		}
		const auto status = reply
			->attribute(QNetworkRequest::HttpStatusCodeAttribute)
			.toInt();
		if (reply->error() != QNetworkReply::NoError && status == 0) {
			const auto err = int(reply->error());
			LOG(("Employee: verify network error code=%1").arg(err));
			reply->deleteLater();
			_reply.clear();
			done(VerifyFailure{ VerifyFailure::Kind::Network });
			return;
		}
		const auto body = reply->readAll();
		reply->deleteLater();
		_reply.clear();
		done(ParseVerifyResponse(status, body));
	};
	QObject::connect(
		reply,
		&QNetworkReply::finished,
		this,
		finishedCallback);
}

void VerifyClient::cancel() {
	if (_reply) {
		_reply->disconnect();
		_reply->abort();
		_reply->deleteLater();
		_reply.clear();
	}
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
