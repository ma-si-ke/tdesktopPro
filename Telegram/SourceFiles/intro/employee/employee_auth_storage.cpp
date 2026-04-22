/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_auth_storage.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <QtCore/QDataStream>

namespace Intro::Employee {
namespace {

constexpr quint32 kMagic = 0x454D5041u; // 'EMPA'
constexpr quint32 kVersion = 1u;

} // namespace

QByteArray SerializeAuthSnapshot(const AuthSnapshot &snap) {
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_15);
	stream << kMagic << kVersion;
	stream << snap.token.toUtf8();
	quint16 bits = 0;
	for (auto i = 0; i != kPermissionCount; ++i) {
		if (snap.permissions[i]) {
			bits |= (quint16(1) << i);
		}
	}
	stream << bits;
	stream << quint32(static_cast<uchar>(snap.backend));
	return bytes;
}

std::optional<AuthSnapshot> DeserializeAuthSnapshot(const QByteArray &bytes) {
	auto stream = QDataStream(bytes);
	stream.setVersion(QDataStream::Qt_5_15);

	quint32 magic = 0;
	quint32 version = 0;
	stream >> magic >> version;
	if (stream.status() != QDataStream::Ok
		|| magic != kMagic
		|| version != kVersion) {
		return std::nullopt;
	}

	QByteArray tokenBytes;
	stream >> tokenBytes;

	quint16 bits = 0;
	stream >> bits;

	quint32 backendRaw = 0;
	stream >> backendRaw;
	if (stream.status() != QDataStream::Ok) {
		return std::nullopt;
	}

	auto snap = AuthSnapshot{};
	snap.token = QString::fromUtf8(tokenBytes);
	for (auto i = 0; i != kPermissionCount; ++i) {
		snap.permissions[i] = ((bits >> i) & 1u) != 0;
	}
	const auto maxBackend = static_cast<quint32>(BackendType::Other);
	snap.backend = (backendRaw <= maxBackend)
		? static_cast<BackendType>(backendRaw)
		: BackendType::Customer;
	return snap;
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
