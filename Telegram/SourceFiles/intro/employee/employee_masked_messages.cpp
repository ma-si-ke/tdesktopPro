/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_masked_messages.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_permissions.h"
#include "data/data_session.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "base/debug_log.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Intro::Employee {
namespace {

constexpr auto kBaseUrl = "http://43.132.171.63:3100";

[[nodiscard]] not_null<QNetworkAccessManager*> Network() {
	static const auto result = new QNetworkAccessManager();
	return result;
}

[[nodiscard]] base::flat_set<MsgId> ParseMaskedIds(
		const QByteArray &body,
		const QString &selfUserId,
		const QString &selfType) {
	auto result = base::flat_set<MsgId>();
	const auto document = QJsonDocument::fromJson(body);
	if (!document.isObject()) {
		return result;
	}
	const auto entries = document.object().value(u"entries"_q).toArray();
	for (const auto &entryValue : entries) {
		const auto entry = entryValue.toObject();
		const auto userId = entry.value(u"userId"_q).toString();
		const auto type = entry.value(u"type"_q).toString();
		// Keep only records for this account's own tgId (multiple staff logins
		// of different types share one tgId), then drop the ones added by my
		// own type — i.e. mask what the OTHER types on my account encrypted.
		if (userId != selfUserId) {
			continue;
		} else if (type == selfType) {
			continue;
		}
		for (const auto &idValue : entry.value(u"messageIds"_q).toArray()) {
			if (const auto id = MsgId(int64(idValue.toDouble()))) {
				result.emplace(id);
			}
		}
	}
	return result;
}

} // namespace

void FetchMaskedMessages(not_null<Main::Session*> session, PeerId peerId) {
	const auto selfType = EmployeeTypeToString(session->account().employeeType());
	if (selfType.isEmpty()) {
		return; // No type -> masking is unavailable; don't query.
	}
	const auto peerTgId = QString::number(peerId.value);
	const auto selfUserId = QString::number(session->userId().bare);

	const auto url = QString::fromLatin1(kBaseUrl)
		+ u"/api/v1/staff/"_q + peerTgId;
	const auto reply = Network()->get(QNetworkRequest(QUrl(url)));
	const auto apply = crl::guard(session.get(), [=](const QByteArray &body) {
		auto ids = ParseMaskedIds(body, selfUserId, selfType);
		LOG(("Employee: masked-messages peer=%1 count=%2"
			).arg(peerTgId).arg(ids.size()));
		session->data().setMaskedMessages(peerId, std::move(ids));
	});
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		const auto status = reply
			->attribute(QNetworkRequest::HttpStatusCodeAttribute)
			.toInt();
		const auto body = reply->readAll();
		reply->deleteLater();
		if (status < 200 || status >= 300) {
			LOG(("Employee: masked-messages http=%1 peer=%2"
				).arg(status).arg(peerTgId));
			return;
		}
		apply(body);
	});
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
