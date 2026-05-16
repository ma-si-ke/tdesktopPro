/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_hidden_folders_client.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/debug_log.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Intro::Employee {

HiddenFoldersResult ParseHiddenFoldersResponse(
		int httpStatus,
		const QByteArray &body) {
	if (httpStatus == 401) {
		LOG(("Employee: hidden-folders 401 invalid token"));
		return HiddenFoldersFailure{
			HiddenFoldersFailure::Kind::InvalidToken,
		};
	}
	if (httpStatus >= 500) {
		LOG(("Employee: hidden-folders server error http=%1"
			).arg(httpStatus));
		return HiddenFoldersFailure{ HiddenFoldersFailure::Kind::Server };
	}
	if (httpStatus >= 400) {
		LOG(("Employee: hidden-folders client error http=%1"
			).arg(httpStatus));
		return HiddenFoldersFailure{ HiddenFoldersFailure::Kind::Server };
	}

	auto parseError = QJsonParseError{};
	const auto doc = QJsonDocument::fromJson(body, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		LOG(("Employee: hidden-folders bad json"));
		return HiddenFoldersFailure{ HiddenFoldersFailure::Kind::BadJson };
	}
	const auto root = doc.object();
	const auto value = root.value(u"hiddenFolderNames"_q);
	if (!value.isArray()) {
		LOG(("Employee: hidden-folders response missing array"));
		return HiddenFoldersFailure{ HiddenFoldersFailure::Kind::BadJson };
	}

	auto names = QStringList();
	for (const auto &entry : value.toArray()) {
		if (entry.isString()) {
			const auto name = entry.toString();
			if (!name.isEmpty()) {
				names.push_back(name);
			}
		}
	}
	return HiddenFoldersSuccess{ std::move(names) };
}

HiddenFoldersClient::HiddenFoldersClient(QObject *parent)
: QObject(parent)
, _nam(std::make_unique<QNetworkAccessManager>()) {
}

HiddenFoldersClient::~HiddenFoldersClient() {
	cancel();
}

void HiddenFoldersClient::fetch(
		BackendType backend,
		QString token,
		Fn<void(HiddenFoldersResult)> done) {
	cancel();

	if (token.isEmpty()) {
		done(HiddenFoldersFailure{
			HiddenFoldersFailure::Kind::InvalidToken,
		});
		return;
	}

	const auto info = BackendInfoFor(backend);
	auto url = QUrl();
	url.setScheme(u"http"_q);
	url.setHost(info.host);
	url.setPort(info.port);
	url.setPath(u"/api/permissions/hidden-folders"_q);

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
			LOG(("Employee: hidden-folders network error code=%1").arg(err));
			reply->deleteLater();
			_reply.clear();
			done(HiddenFoldersFailure{
				HiddenFoldersFailure::Kind::Network,
			});
			return;
		}
		const auto body = reply->readAll();
		reply->deleteLater();
		_reply.clear();
		done(ParseHiddenFoldersResponse(status, body));
	};
	QObject::connect(
		reply,
		&QNetworkReply::finished,
		this,
		finishedCallback);
}

void HiddenFoldersClient::cancel() {
	if (_reply) {
		_reply->disconnect();
		_reply->abort();
		_reply->deleteLater();
		_reply.clear();
	}
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
