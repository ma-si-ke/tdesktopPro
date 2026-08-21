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

	// Mode defaults to Blacklist when absent or unrecognized: a missing or
	// partial policy should fail open (show all) rather than hide everything,
	// which an empty whitelist would do.
	auto mode = HiddenFoldersMode::Blacklist;
	const auto modeValue = root.value(u"mode"_q);
	if (modeValue.isString()) {
		const auto text = modeValue.toString();
		if (text == u"whitelist"_q) {
			mode = HiddenFoldersMode::Whitelist;
		} else if (text != u"blacklist"_q) {
			LOG(("Employee: hidden-folders unknown mode '%1', using blacklist"
				).arg(text));
		}
	}

	const auto foldersValue = root.value(u"folders"_q);
	if (!foldersValue.isArray()) {
		LOG(("Employee: hidden-folders response missing folders array"));
		return HiddenFoldersFailure{ HiddenFoldersFailure::Kind::BadJson };
	}

	auto folders = std::vector<HiddenFolderEntry>();
	for (const auto &entry : foldersValue.toArray()) {
		if (!entry.isObject()) {
			continue;
		}
		const auto object = entry.toObject();
		const auto name = object.value(u"name"_q).toString();
		if (name.isEmpty()) {
			continue;
		}
		folders.push_back(HiddenFolderEntry{
			.name = name,
			.priority = object.value(u"priority"_q).toInt(),
		});
	}
	return HiddenFoldersSuccess{ .mode = mode, .folders = std::move(folders) };
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
	url.setScheme(u"https"_q);
	url.setHost(info.host);
	if (info.port) {
		url.setPort(info.port);
	}
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
