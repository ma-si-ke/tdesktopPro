/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "audit/audit_reporter.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "audit/audit_event.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "storage/storage_account.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "intro/employee/employee_config.h"
#include "intro/employee/employee_permissions.h"
#include "core/version.h"
#include "base/debug_log.h"
#include "base/flat_set.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QDateTime>
#include <QtCore/QSysInfo>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>

namespace Audit {
namespace {

constexpr auto kUploadIntervalMs = crl::time(30 * 1000);
constexpr auto kMaxBatch = 200;

} // namespace

Reporter::Reporter(not_null<Main::Session*> session)
: _session(session)
, _nam(std::make_unique<QNetworkAccessManager>())
, _timer([=] { uploadTick(); }) {
	const auto machineId = QSysInfo::machineUniqueId();
	_deviceId = machineId.isEmpty()
		? u"tdesktop-unknown"_q
		: (u"tdesktop-"_q + QString::fromLatin1(machineId.toHex()));

	load();

	_session->data().newItemAdded(
	) | rpl::on_next([=](not_null<HistoryItem*> item) {
		if (item->out() && item->isSending()) {
			recordSent(item);
		}
	}, _lifetime);

	if (!_pending.empty()) {
		scheduleUpload();
	}
}

Reporter::~Reporter() {
	if (_reply) {
		_reply->disconnect();
		_reply->abort();
		_reply->deleteLater();
		_reply.clear();
	}
}

void Reporter::recordSent(not_null<HistoryItem*> item) {
	enqueue(BuildSentEvent(item));
}

void Reporter::recordEdit(
		not_null<HistoryItem*> item,
		const TextWithEntities &before,
		const TextWithEntities &after) {
	enqueue(BuildEditedEvent(item, before, after));
}

void Reporter::recordDelete(not_null<HistoryItem*> item, bool revoke) {
	enqueue(BuildDeletedEvent(item, revoke));
}

void Reporter::enqueue(QJsonObject event) {
	const auto seq = _nextSeq++;
	event.insert(u"seq"_q, double(seq));
	event.insert(u"eventId"_q, _deviceId + u":"_q + QString::number(seq));
	event.insert(u"occurredAt"_q, double(QDateTime::currentMSecsSinceEpoch()));
	_pending.push_back(std::move(event));
	persist();
	scheduleUpload();
}

void Reporter::load() {
	const auto bytes = _session->local().readAuditQueue();
	if (bytes.isEmpty()) {
		return;
	}
	const auto document = QJsonDocument::fromJson(bytes);
	if (!document.isObject()) {
		return;
	}
	const auto root = document.object();
	const auto nextSeq = uint64(root.value(u"nextSeq"_q).toDouble(1));
	_nextSeq = std::max(nextSeq, uint64(1));
	for (const auto &value : root.value(u"events"_q).toArray()) {
		if (value.isObject()) {
			_pending.push_back(value.toObject());
		}
	}
	LOG(("Audit: loaded %1 pending events").arg(_pending.size()));
}

void Reporter::persist() {
	auto events = QJsonArray();
	for (const auto &event : _pending) {
		events.append(event);
	}
	auto root = QJsonObject();
	root.insert(u"nextSeq"_q, double(_nextSeq));
	root.insert(u"events"_q, events);
	const auto bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
	_session->local().writeAuditQueue(bytes);
}

bool Reporter::authorized() const {
	return _session->account().employeePermissions().authorized();
}

void Reporter::scheduleUpload() {
	if (_uploading || _timer.isActive() || _pending.empty()) {
		return;
	}
	_timer.callOnce(kUploadIntervalMs);
}

void Reporter::uploadTick() {
	if (_uploading || _pending.empty()) {
		return;
	} else if (!authorized()) {
		scheduleUpload();
		return;
	}

	const auto count = std::min(int(_pending.size()), kMaxBatch);
	auto events = QJsonArray();
	for (auto i = 0; i != count; ++i) {
		events.append(_pending[i]);
	}

	auto device = QJsonObject();
	device.insert(u"deviceId"_q, _deviceId);
	device.insert(u"clientVersion"_q, AppVersion);
	device.insert(u"accountUserId"_q, QString::number(_session->userId().bare));

	auto root = QJsonObject();
	root.insert(u"device"_q, device);
	root.insert(u"sentAt"_q, double(QDateTime::currentMSecsSinceEpoch()));
	root.insert(u"events"_q, events);

	const auto info = Intro::Employee::BackendInfoFor(
		_session->account().employeeBackend());
	auto url = QUrl();
	url.setScheme(u"http"_q);
	url.setHost(info.host);
	url.setPort(info.port);
	url.setPath(u"/api/audit/events"_q);

	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader(
		"Authorization",
		(u"Bearer "_q
			+ _session->account().employeePermissions().token()).toLatin1());

	const auto payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
	_uploading = true;
	_reply = _nam->post(request, payload);
	const auto reply = _reply.data();
	const auto finishedCallback = [this, reply] {
		if (!reply || reply != _reply.data()) {
			return;
		}
		const auto status = reply
			->attribute(QNetworkRequest::HttpStatusCodeAttribute)
			.toInt();
		if (reply->error() != QNetworkReply::NoError && status == 0) {
			LOG(("Audit: upload network error code=%1").arg(int(reply->error())));
			reply->deleteLater();
			_reply.clear();
			_uploading = false;
			scheduleUpload();
			return;
		}
		const auto body = reply->readAll();
		reply->deleteLater();
		_reply.clear();
		_uploading = false;
		onUploaded(status, body);
	};
	QObject::connect(
		reply,
		&QNetworkReply::finished,
		reply,
		finishedCallback);
}

void Reporter::onUploaded(int status, const QByteArray &body) {
	if (status == 401) {
		LOG(("Audit: upload 401; keeping queue"));
		scheduleUpload();
		return;
	} else if (status < 200 || status >= 300) {
		LOG(("Audit: upload http=%1; keeping queue").arg(status));
		scheduleUpload();
		return;
	}

	auto parseError = QJsonParseError{};
	const auto document = QJsonDocument::fromJson(body, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("Audit: upload bad json; keeping queue"));
		scheduleUpload();
		return;
	}
	const auto root = document.object();

	auto handled = base::flat_set<QString>();
	for (const auto &value : root.value(u"acceptedEventIds"_q).toArray()) {
		handled.emplace(value.toString());
	}
	for (const auto &value : root.value(u"rejectedEvents"_q).toArray()) {
		handled.emplace(value.toObject().value(u"eventId"_q).toString());
	}

	if (!handled.empty()) {
		_pending.erase(
			std::remove_if(
				_pending.begin(),
				_pending.end(),
				[&](const QJsonObject &event) {
					return handled.contains(
						event.value(u"eventId"_q).toString());
				}),
			_pending.end());
		persist();
		LOG(("Audit: uploaded ok, %1 left").arg(_pending.size()));
	}
	scheduleUpload();
}

} // namespace Audit

#endif // TDESKTOP_EMPLOYEE_MODE
