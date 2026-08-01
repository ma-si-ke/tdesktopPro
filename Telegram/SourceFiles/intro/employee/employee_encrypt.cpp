/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_encrypt.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_permissions.h"
#include "core/service_config.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
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

[[nodiscard]] not_null<QNetworkAccessManager*> Network() {
	static const auto result = new QNetworkAccessManager();
	return result;
}

} // namespace

void EncryptSelectedMessages(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		const MessageIdsList &ids) {
	// Backstop: the encrypt UI (top-bar button, context-menu items) is already
	// disabled when this permission is off, so this is defense-in-depth. Silent
	// return because a disabled control can't legitimately reach here.
	if (!controller->session().account().employeePermissions().has(
			PermissionKey::MsgEncrypt)) {
		return;
	}
	if (controller->session().account().employeeType()
			== EmployeeType::None) {
		controller->showToast(u"不可用"_q);
		return;
	}
	auto messageIds = QJsonArray();
	for (const auto &fullId : ids) {
		if (fullId.peer == peer->id) {
			messageIds.append(qint64(fullId.msg.bare));
		}
	}
	if (messageIds.isEmpty()) {
		return;
	}

	const auto session = &controller->session();
	const auto peerTgId = QString::number(peer->id.value);
	const auto selfUserId = QString::number(session->userId().bare);
	const auto type = session->account().employeeType();

	const auto payload = QJsonDocument(QJsonObject{
		{ u"type"_q, EmployeeTypeToString(type) },
		{ u"messageIds"_q, messageIds },
	}).toJson(QJsonDocument::Compact);

	const auto url = Core::ServiceUrl()
		+ u"/api/v1/staff/"_q + peerTgId + '/' + selfUserId;
	auto request = QNetworkRequest(QUrl(url));
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);

	const auto count = messageIds.size();
	const auto show = controller->uiShow();
	const auto reply = Network()->post(request, payload);
	QObject::connect(reply, &QNetworkReply::finished, reply, [=] {
		const auto status = reply
			->attribute(QNetworkRequest::HttpStatusCodeAttribute)
			.toInt();
		reply->deleteLater();
		const auto ok = (status >= 200 && status < 300);
		LOG(("Employee: encrypt %1 msgs http=%2").arg(count).arg(status));
		show->showToast(ok ? u"已加密"_q : u"加密失败"_q);
	});
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
