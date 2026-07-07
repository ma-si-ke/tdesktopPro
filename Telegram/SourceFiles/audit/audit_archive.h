/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/flat_set.h"

#include <rpl/event_stream.h>
#include <rpl/producer.h>

#include <QtCore/QJsonObject>
#include <QtCore/QPointer>

#include <map>
#include <memory>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace Main {
class Session;
} // namespace Main

namespace Audit {

struct ArchiveDay {
	qint32 day = 0; // Julian day number, local date.
	int count = 0;
	qint64 chars = 0;
	int uploaded = 0;
};

struct ArchiveHour {
	int hour = 0; // 0..23, local time.
	int count = 0;
	qint64 chars = 0;
	int uploaded = 0;
};

struct ArchiveUploadResult {
	bool ok = false;
	int accepted = 0;
	int failed = 0;
	QString error;
};

// A permanent, per-day-segmented local copy of every audit event. Unlike the
// Reporter's automatic upload queue, records kept here are NEVER removed when
// an automatic upload succeeds; they are only ever marked as uploaded when the
// user manually pushes them to the import endpoint. Each day is stored in its
// own encrypted file so appending only rewrites the current day, and a small
// index (day -> {fileKey, count, chars, uploaded}) drives the management UI.
class ArchiveStore final {
public:
	explicit ArchiveStore(not_null<Main::Session*> session);
	~ArchiveStore();

	void append(const QJsonObject &event);

	[[nodiscard]] std::vector<ArchiveDay> days() const; // newest day first
	[[nodiscard]] std::vector<QJsonObject> dayEvents(qint32 day) const;
	[[nodiscard]] std::vector<ArchiveHour> dayHours(qint32 day) const;
	[[nodiscard]] std::vector<QJsonObject> eventsForHours(
		qint32 day,
		const base::flat_set<int> &hours) const;

	// Permanently drops every event in the given hour buckets of a day (used
	// to cap local disk usage). Removes the day entirely when it becomes empty.
	// Returns the number of records removed.
	int deleteHours(qint32 day, const base::flat_set<int> &hours);

	[[nodiscard]] rpl::producer<> changes() const;

	// Uploads the given events to /api/audit/events/import in chunks and marks
	// accepted ones as uploaded. Records are retained regardless of outcome.
	void upload(
		std::vector<QJsonObject> events,
		Fn<void(ArchiveUploadResult)> done);
	[[nodiscard]] bool uploading() const;

private:
	struct DayData {
		quint64 fileKey = 0;
		int count = 0;
		qint64 chars = 0;
		int uploaded = 0;
	};

	void load();
	void persistIndex();
	void ensureCached(qint32 day);
	void writeDayFile(qint32 day, const std::vector<QJsonObject> &events);
	void removeDay(qint32 day);
	void markUploaded(
		const std::vector<qint32> &days,
		const base::flat_set<QString> &acceptedIds,
		qint64 uploadedAt);

	void sendNextChunk();
	void onChunkUploaded(int status, const QByteArray &body);
	void finishUpload(bool ok, const QString &error);

	const not_null<Main::Session*> _session;
	std::map<qint32, DayData> _index;

	qint32 _cachedDay = 0;
	std::vector<QJsonObject> _cachedEvents;

	std::unique_ptr<QNetworkAccessManager> _nam;
	QPointer<QNetworkReply> _reply;

	std::vector<QJsonObject> _uploadEvents;
	std::vector<qint32> _uploadDays;
	base::flat_set<QString> _uploadAccepted;
	int _uploadOffset = 0;
	int _uploadLastCount = 0;
	int _uploadRejected = 0;
	Fn<void(ArchiveUploadResult)> _uploadDone;

	rpl::event_stream<> _changes;

};

} // namespace Audit

#endif // TDESKTOP_EMPLOYEE_MODE
