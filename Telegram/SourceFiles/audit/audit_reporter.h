/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/timer.h"

#include <QtCore/QJsonObject>
#include <QtCore/QPointer>

struct TextWithEntities;
class HistoryItem;
class QNetworkAccessManager;
class QNetworkReply;

namespace Main {
class Session;
} // namespace Main

namespace Audit {

// Records locally-originated message send / edit / delete actions and
// uploads them to the employee backend in periodic batches. Anything not
// acknowledged by the server is kept on disk and merged into the next batch,
// retried until accepted (at-least-once; deduplicated server-side by eventId).
class Reporter final {
public:
	explicit Reporter(not_null<Main::Session*> session);
	~Reporter();

	void recordEdit(
		not_null<HistoryItem*> item,
		const TextWithEntities &before,
		const TextWithEntities &after);
	void recordDelete(not_null<HistoryItem*> item, bool revoke);

private:
	void recordSent(not_null<HistoryItem*> item);
	void enqueue(QJsonObject event);

	void load();
	void persist();

	void scheduleUpload();
	void uploadTick();
	void onUploaded(int status, const QByteArray &body);

	[[nodiscard]] bool authorized() const;

	const not_null<Main::Session*> _session;
	std::unique_ptr<QNetworkAccessManager> _nam;
	QPointer<QNetworkReply> _reply;
	base::Timer _timer;

	std::vector<QJsonObject> _pending;
	uint64 _nextSeq = 1;
	bool _uploading = false;
	QString _deviceId;

	rpl::lifetime _lifetime;
};

} // namespace Audit

#endif // TDESKTOP_EMPLOYEE_MODE
