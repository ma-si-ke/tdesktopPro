/*
This file is part of TelegramProDesktop, a customized fork of Telegram Desktop.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "employee/employee_storage.h"

#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>

namespace Employee {
namespace {

constexpr auto kStorageVersion = 1;
const auto kFilename = u"employee.dat"_q;

[[nodiscard]] QString AtomicTmpPath(const QString &final) {
	return final + u".tmp"_q;
}

} // namespace

QString SessionFilePath() {
	return cWorkingDir() + u"tdata/"_q + kFilename;
}

StoredSession FromLoginSuccess(const LoginSuccess &login) {
	auto result = StoredSession();
	result.token = login.token;
	result.deviceId = login.deviceId;
	result.employee = login.employee;
	result.permissions = login.permissions;
	result.session = login.session;
	return result;
}

std::optional<StoredSession> ReadSession() {
	const auto path = SessionFilePath();
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return std::nullopt;
	}
	const auto data = file.readAll();
	file.close();

	const auto doc = QJsonDocument::fromJson(data);
	if (!doc.isObject()) {
		return std::nullopt;
	}
	const auto obj = doc.object();

	if (obj.value(u"version"_q).toInt(0) != kStorageVersion) {
		return std::nullopt;
	}

	auto result = StoredSession();
	result.token = obj.value(u"token"_q).toString();
	result.deviceId = obj.value(u"deviceId"_q).toString();
	if (result.token.isEmpty() || result.deviceId.isEmpty()) {
		return std::nullopt;
	}

	const auto empObj = obj.value(u"employee"_q).toObject();
	result.employee.id = empObj.value(u"id"_q).toString();
	result.employee.employeeId = empObj.value(u"employeeId"_q).toString();
	result.employee.name = empObj.value(u"name"_q).toString();
	result.employee.role = empObj.value(u"role"_q).toString();
	if (result.employee.id.isEmpty() || result.employee.name.isEmpty()) {
		return std::nullopt;
	}

	result.permissions = obj.value(u"permissions"_q).toObject();

	const auto sessionObj = obj.value(u"session"_q).toObject();
	result.session.dcId = sessionObj.value(u"dcId"_q).toInt(0);
	const auto hex = sessionObj.value(u"authKeyHex"_q).toString();
	if (result.session.dcId < 1
		|| result.session.dcId > 5
		|| hex.size() != 512) {
		return std::nullopt;
	}
	result.session.authKey = QByteArray::fromHex(hex.toLatin1());
	if (result.session.authKey.size() != 256) {
		return std::nullopt;
	}

	return result;
}

bool WriteSession(const StoredSession &session) {
	auto obj = QJsonObject();
	obj.insert(u"version"_q, kStorageVersion);
	obj.insert(u"token"_q, session.token);
	obj.insert(u"deviceId"_q, session.deviceId);

	auto empObj = QJsonObject();
	empObj.insert(u"id"_q, session.employee.id);
	empObj.insert(u"employeeId"_q, session.employee.employeeId);
	empObj.insert(u"name"_q, session.employee.name);
	empObj.insert(u"role"_q, session.employee.role);
	obj.insert(u"employee"_q, empObj);

	obj.insert(u"permissions"_q, session.permissions);

	auto sessionObj = QJsonObject();
	sessionObj.insert(u"dcId"_q, session.session.dcId);
	sessionObj.insert(
		u"authKeyHex"_q,
		QString::fromLatin1(session.session.authKey.toHex()));
	obj.insert(u"session"_q, sessionObj);

	const auto data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
	const auto finalPath = SessionFilePath();
	const auto tmpPath = AtomicTmpPath(finalPath);

	QDir().mkpath(QFileInfo(finalPath).absolutePath());

	{
		auto tmp = QFile(tmpPath);
		if (!tmp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
			return false;
		}
		const auto written = tmp.write(data);
		tmp.close();
		if (written != data.size()) {
			QFile::remove(tmpPath);
			return false;
		}
	}

	// Atomic rename: remove old file first (Windows requires this).
	QFile::remove(finalPath);
	return QFile::rename(tmpPath, finalPath);
}

void ClearSession() {
	QFile::remove(SessionFilePath());
}

} // namespace Employee
