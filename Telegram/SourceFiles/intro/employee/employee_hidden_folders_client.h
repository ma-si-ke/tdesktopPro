/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_config.h"

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QStringList>
#include <variant>

class QNetworkAccessManager;
class QNetworkReply;

namespace Intro::Employee {

struct HiddenFoldersSuccess {
	QStringList names;
};

struct HiddenFoldersFailure {
	enum class Kind { Network, Server, InvalidToken, BadJson };
	Kind kind = Kind::Network;
};

using HiddenFoldersResult
	= std::variant<HiddenFoldersSuccess, HiddenFoldersFailure>;

// Pure parser: safe to unit-test without network.
[[nodiscard]] HiddenFoldersResult ParseHiddenFoldersResponse(
	int httpStatus,
	const QByteArray &body);

class HiddenFoldersClient final : public QObject {
public:
	explicit HiddenFoldersClient(QObject *parent = nullptr);
	~HiddenFoldersClient();

	void fetch(
		BackendType backend,
		QString token,
		Fn<void(HiddenFoldersResult)> done);

	void cancel();

private:
	std::unique_ptr<QNetworkAccessManager> _nam;
	QPointer<QNetworkReply> _reply;
};

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
