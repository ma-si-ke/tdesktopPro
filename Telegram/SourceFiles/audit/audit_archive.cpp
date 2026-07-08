/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "audit/audit_archive.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "audit/audit_event.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "storage/storage_account.h"
#include "intro/employee/employee_permissions.h"
#include "base/debug_log.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QDateTime>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <algorithm>
#include <array>

namespace Audit {
namespace {

constexpr auto kImportChunk = 5000;
constexpr auto kImportPath = "/api/audit/events/import";

[[nodiscard]] qint64 OccurredAt(const QJsonObject &event) {
	return qint64(event.value(u"occurredAt"_q).toDouble());
}

[[nodiscard]] qint32 DayOf(qint64 ms) {
	if (!ms) {
		return 0;
	}
	return qint32(QDateTime::fromMSecsSinceEpoch(ms).date().toJulianDay());
}

[[nodiscard]] QString EventId(const QJsonObject &event) {
	return event.value(u"eventId"_q).toString();
}

[[nodiscard]] QString PrimaryText(const QJsonObject &event) {
	const auto payload = event.value(u"payload"_q).toObject();
	const auto type = event.value(u"type"_q).toString();
	if (type == u"message_edited"_q) {
		return payload.value(u"after"_q).toObject().value(u"text"_q).toString();
	} else if (type == u"message_deleted"_q) {
		return payload.value(u"original"_q).toObject()
			.value(u"content"_q).toObject().value(u"text"_q).toString();
	}
	return payload.value(u"content"_q).toObject().value(u"text"_q).toString();
}

[[nodiscard]] int CharCount(const QJsonObject &event) {
	return int(PrimaryText(event).size());
}

[[nodiscard]] QByteArray SerializeEvents(
		const std::vector<QJsonObject> &events) {
	auto array = QJsonArray();
	for (const auto &event : events) {
		array.append(event);
	}
	auto root = QJsonObject();
	root.insert(u"events"_q, array);
	return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

[[nodiscard]] std::vector<QJsonObject> ParseEvents(const QByteArray &bytes) {
	auto result = std::vector<QJsonObject>();
	if (bytes.isEmpty()) {
		return result;
	}
	const auto document = QJsonDocument::fromJson(bytes);
	if (!document.isObject()) {
		return result;
	}
	const auto array = document.object().value(u"events"_q).toArray();
	result.reserve(array.size());
	for (const auto &value : array) {
		if (value.isObject()) {
			result.push_back(value.toObject());
		}
	}
	return result;
}

} // namespace

ArchiveStore::ArchiveStore(not_null<Main::Session*> session)
: _session(session)
, _nam(std::make_unique<QNetworkAccessManager>()) {
}

ArchiveStore::~ArchiveStore() {
	if (_reply) {
		_reply->disconnect();
		_reply->abort();
		_reply->deleteLater();
		_reply.clear();
	}
}

void ArchiveStore::ensureCached(qint32 day) {
	if (_cachedDay == day) {
		return;
	}
	_cachedEvents = ParseEvents(_session->local().readAuditArchiveDay(day));
	_cachedDay = day;
}

void ArchiveStore::append(const QJsonObject &event) {
	const auto day = DayOf(OccurredAt(event));
	if (!day) {
		return;
	}
	ensureCached(day);
	_cachedEvents.push_back(event);
	_session->local().writeAuditArchiveDay(day, SerializeEvents(_cachedEvents));
	_changes.fire({});
}

std::vector<ArchiveDay> ArchiveStore::days() const {
	auto result = std::vector<ArchiveDay>();
	for (const auto day : _session->local().auditArchiveDays()) {
		const auto events = dayEvents(day);
		auto info = ArchiveDay{ day };
		info.count = int(events.size());
		for (const auto &event : events) {
			info.chars += CharCount(event);
			if (event.contains(u"uploadedAt"_q)) {
				++info.uploaded;
			}
		}
		if (info.count > 0) {
			result.push_back(info);
		}
	}
	std::sort(result.begin(), result.end(), [](
			const ArchiveDay &a,
			const ArchiveDay &b) {
		return a.day > b.day;
	});
	return result;
}

std::vector<QJsonObject> ArchiveStore::dayEvents(qint32 day) const {
	if (day == _cachedDay) {
		return _cachedEvents;
	}
	return ParseEvents(_session->local().readAuditArchiveDay(day));
}

std::vector<ArchiveHour> ArchiveStore::dayHours(qint32 day) const {
	const auto events = dayEvents(day);
	auto buckets = std::array<ArchiveHour, 24>();
	for (auto hour = 0; hour != 24; ++hour) {
		buckets[hour].hour = hour;
	}
	for (const auto &event : events) {
		const auto hour = QDateTime::fromMSecsSinceEpoch(OccurredAt(event))
			.time().hour();
		if (hour < 0 || hour > 23) {
			continue;
		}
		++buckets[hour].count;
		buckets[hour].chars += CharCount(event);
		if (event.contains(u"uploadedAt"_q)) {
			++buckets[hour].uploaded;
		}
	}
	auto result = std::vector<ArchiveHour>();
	for (auto hour = 0; hour != 24; ++hour) {
		if (buckets[hour].count > 0) {
			result.push_back(buckets[hour]);
		}
	}
	return result;
}

std::vector<QJsonObject> ArchiveStore::eventsForHours(
		qint32 day,
		const base::flat_set<int> &hours) const {
	auto result = std::vector<QJsonObject>();
	if (hours.empty()) {
		return result;
	}
	for (const auto &event : dayEvents(day)) {
		const auto hour = QDateTime::fromMSecsSinceEpoch(OccurredAt(event))
			.time().hour();
		if (hours.contains(hour)) {
			result.push_back(event);
		}
	}
	return result;
}

int ArchiveStore::deleteHours(qint32 day, const base::flat_set<int> &hours) {
	if (hours.empty()) {
		return 0;
	}
	auto events = dayEvents(day);
	const auto before = int(events.size());
	events.erase(
		std::remove_if(events.begin(), events.end(), [&](
				const QJsonObject &event) {
			const auto hour = QDateTime::fromMSecsSinceEpoch(OccurredAt(event))
				.time().hour();
			return hours.contains(hour);
		}),
		events.end());
	const auto removed = before - int(events.size());
	if (!removed) {
		return 0;
	}
	if (events.empty()) {
		_session->local().removeAuditArchiveDay(day);
	} else {
		_session->local().writeAuditArchiveDay(day, SerializeEvents(events));
	}
	if (_cachedDay == day) {
		_cachedDay = 0;
		_cachedEvents.clear();
	}
	_changes.fire({});
	return removed;
}

rpl::producer<> ArchiveStore::changes() const {
	return _changes.events();
}

void ArchiveStore::markUploaded(
		const std::vector<qint32> &days,
		const base::flat_set<QString> &acceptedIds,
		qint64 uploadedAt) {
	auto changedAny = false;
	for (const auto day : days) {
		auto events = dayEvents(day);
		auto changed = false;
		for (auto &event : events) {
			if (!event.contains(u"uploadedAt"_q)
					&& acceptedIds.contains(EventId(event))) {
				event.insert(u"uploadedAt"_q, double(uploadedAt));
				changed = true;
			}
		}
		if (changed) {
			_session->local().writeAuditArchiveDay(day, SerializeEvents(events));
			if (_cachedDay == day) {
				_cachedDay = 0;
				_cachedEvents.clear();
			}
			changedAny = true;
		}
	}
	if (changedAny) {
		_changes.fire({});
	}
}

bool ArchiveStore::uploading() const {
	return !_uploadEvents.empty();
}

void ArchiveStore::upload(
		std::vector<QJsonObject> events,
		Fn<void(ArchiveUploadResult)> done) {
	if (uploading()) {
		if (done) {
			done({ .ok = false, .error = u"已有上传任务进行中，请稍候"_q });
		}
		return;
	} else if (events.empty()) {
		if (done) {
			done({ .ok = true });
		}
		return;
	} else if (!_session->account().employeePermissions().authorized()) {
		if (done) {
			done({ .ok = false, .error = u"未授权，无法上传"_q });
		}
		return;
	}

	_uploadEvents = std::move(events);
	_uploadOffset = 0;
	_uploadLastCount = 0;
	_uploadRejected = 0;
	_uploadAccepted.clear();
	_uploadDone = std::move(done);

	_uploadDays.clear();
	auto seen = base::flat_set<qint32>();
	for (const auto &event : _uploadEvents) {
		const auto day = DayOf(OccurredAt(event));
		if (day && seen.emplace(day).second) {
			_uploadDays.push_back(day);
		}
	}

	sendNextChunk();
}

void ArchiveStore::sendNextChunk() {
	if (_uploadOffset >= int(_uploadEvents.size())) {
		finishUpload(true, QString());
		return;
	}
	const auto count = std::min(
		kImportChunk,
		int(_uploadEvents.size()) - _uploadOffset);
	_uploadLastCount = count;

	auto array = QJsonArray();
	for (auto i = 0; i != count; ++i) {
		// Strip the local-only marker so the payload matches the /events
		// schema exactly (the import endpoint validates fields the same way).
		auto event = _uploadEvents[_uploadOffset + i];
		event.remove(u"uploadedAt"_q);
		array.append(event);
	}

	const auto now = QDateTime::currentMSecsSinceEpoch();
	auto root = QJsonObject();
	root.insert(
		u"batchId"_q,
		DeviceId() + u":import:"_q + QString::number(now));
	root.insert(u"device"_q, BuildDeviceObject(_session));
	root.insert(u"sentAt"_q, double(now));
	root.insert(u"events"_q, array);

	const auto url = BackendUrl(_session, QString::fromLatin1(kImportPath));
	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader(
		"Authorization",
		(u"Bearer "_q
			+ _session->account().employeePermissions().token()).toLatin1());

	const auto payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
	_reply = _nam->post(request, payload);
	const auto reply = _reply.data();
	QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply] {
		if (!reply || reply != _reply.data()) {
			return;
		}
		const auto status = reply
			->attribute(QNetworkRequest::HttpStatusCodeAttribute)
			.toInt();
		if (reply->error() != QNetworkReply::NoError && status == 0) {
			LOG(("Audit: import network error code=%1")
				.arg(int(reply->error())));
			reply->deleteLater();
			_reply.clear();
			finishUpload(false, u"网络错误，请检查连接后重试"_q);
			return;
		}
		const auto body = reply->readAll();
		reply->deleteLater();
		_reply.clear();
		onChunkUploaded(status, body);
	});
}

void ArchiveStore::onChunkUploaded(int status, const QByteArray &body) {
	if (status < 200 || status >= 300) {
		const auto document = QJsonDocument::fromJson(body);
		const auto error = document.isObject()
			? document.object().value(u"error"_q).toString()
			: QString();
		LOG(("Audit: import http=%1 error=%2").arg(status).arg(error));
		finishUpload(false, error.isEmpty()
			? (u"上传失败 (HTTP "_q + QString::number(status) + u")"_q)
			: (u"上传失败: "_q + error));
		return;
	}

	const auto document = QJsonDocument::fromJson(body);
	if (document.isObject()) {
		const auto root = document.object();
		for (const auto &value : root.value(u"acceptedEventIds"_q).toArray()) {
			_uploadAccepted.emplace(value.toString());
		}
		_uploadRejected += int(root.value(u"rejectedEvents"_q).toArray().size());
	}

	_uploadOffset += _uploadLastCount;
	sendNextChunk();
}

void ArchiveStore::finishUpload(bool ok, const QString &error) {
	auto result = ArchiveUploadResult();
	result.ok = ok;
	result.accepted = int(_uploadAccepted.size());
	result.failed = _uploadRejected;
	result.error = error;

	if (!_uploadAccepted.empty()) {
		markUploaded(
			_uploadDays,
			_uploadAccepted,
			QDateTime::currentMSecsSinceEpoch());
	}

	auto done = std::move(_uploadDone);
	_uploadDone = nullptr;
	_uploadEvents.clear();
	_uploadDays.clear();
	_uploadAccepted.clear();
	_uploadOffset = 0;
	_uploadLastCount = 0;
	_uploadRejected = 0;

	if (done) {
		done(result);
	}
}

} // namespace Audit

#endif // TDESKTOP_EMPLOYEE_MODE
