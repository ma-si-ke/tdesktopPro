/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_auth.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/debug_log.h"
#include "lang/lang_keys.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Intro::Employee {
namespace {

constexpr auto kAuthKeyHexChars = 512;  // 256 bytes * 2

[[nodiscard]] AuthFailure MakeFailure(
		AuthFailure::Kind kind,
		const QString &localized) {
	return AuthFailure{ kind, localized };
}

[[nodiscard]] AuthResult MakeBadJson() {
	LOG(("Employee: bad json"));
	return MakeFailure(
		AuthFailure::Kind::BadJson,
		tr::lng_employee_err_bad_json(tr::now));
}

} // namespace

AuthResult ParseAuthResponse(int httpStatus, const QByteArray &body) {
	if (httpStatus >= 500) {
		LOG(("Employee: server error http=%1").arg(httpStatus));
		return MakeFailure(
			AuthFailure::Kind::Http5xx,
			tr::lng_employee_err_server(tr::now));
	}
	auto parseError = QJsonParseError{};
	const auto doc = QJsonDocument::fromJson(body, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		return MakeBadJson();
	}
	const auto root = doc.object();

	// Business error codes from our backend.
	if (root.contains("code")) {
		const auto code = root.value("code").toString();
		if (code == u"ALREADY_ONLINE"_q) {
			LOG(("Employee: already online"));
			return MakeFailure(
				AuthFailure::Kind::AlreadyOnline,
				tr::lng_employee_err_already(tr::now));
		} else if (code == u"NOT_BOUND"_q) {
			LOG(("Employee: not bound"));
			return MakeFailure(
				AuthFailure::Kind::NotBound,
				tr::lng_employee_err_not_bound(tr::now));
		}
	}

	if (httpStatus >= 400) {
		LOG(("Employee: auth denied http=%1").arg(httpStatus));
		return MakeFailure(
			AuthFailure::Kind::Http4xx,
			tr::lng_employee_err_auth(tr::now));
	}
	if (httpStatus < 200 || httpStatus >= 300) {
		return MakeBadJson();
	}

	const auto session = root.value("tdesktopSession").toObject();
	if (session.isEmpty()) {
		return MakeBadJson();
	}
	const auto dcId = session.value("dcId").toInt(-1);
	const auto authKeyHex = session.value("authKeyHex").toString();
	const auto userIdRaw = session.value("userId");
	if (dcId <= 0 || authKeyHex.size() != kAuthKeyHexChars) {
		if (authKeyHex.size() != kAuthKeyHexChars) {
			LOG(("Employee: authkey length invalid"));
		}
		return MakeBadJson();
	}

	const auto keyBytes = QByteArray::fromHex(authKeyHex.toLatin1());
	if (keyBytes.size() != 256) {
		LOG(("Employee: authkey hex decode failed"));
		return MakeBadJson();
	}

	const auto userId = UserId(userIdRaw.toVariant().toLongLong());
	if (!userId.bare) {
		return MakeBadJson();
	}

	return AuthSuccess{
		MTP::DcId(dcId),
		keyBytes,
		userId,
	};
}

AuthClient::AuthClient(QObject *parent)
: QObject(parent)
, _nam(std::make_unique<QNetworkAccessManager>()) {
}

AuthClient::~AuthClient() {
	cancel();
}

void AuthClient::login(
		BackendType backend,
		QString username,
		QString password,
		Fn<void(AuthResult)> done) {
	cancel();

	const auto info = BackendInfoFor(backend);
	auto url = QUrl();
	url.setScheme(u"http"_q);
	url.setHost(info.host);
	url.setPort(info.port);
	url.setPath(u"/api/auth/login"_q);

	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);

	auto body = QJsonObject();
	body.insert(u"username"_q, username);
	body.insert(u"password"_q, password);
	const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

	_reply = _nam->post(request, payload);
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
			LOG(("Employee: network error code=%1").arg(err));
			reply->deleteLater();
			_reply.clear();
			done(AuthFailure{
				AuthFailure::Kind::Network,
				tr::lng_employee_err_network(tr::now),
			});
			return;
		}
		const auto body = reply->readAll();
		reply->deleteLater();
		_reply.clear();
		done(ParseAuthResponse(status, body));
	};
	QObject::connect(
		reply,
		&QNetworkReply::finished,
		this,
		finishedCallback);
}

void AuthClient::cancel() {
	if (_reply) {
		_reply->disconnect();
		_reply->abort();
		_reply->deleteLater();
		_reply.clear();
	}
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
