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
constexpr quint32 kVersionV1 = 1u;
constexpr quint32 kVersionV2 = 2u;

} // namespace

QByteArray SerializeAuthSnapshot(const AuthSnapshot &snap) {
	auto bytes = QByteArray();
	auto stream = QDataStream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_15);
	stream << kMagic << kVersionV2;
	stream << snap.token.toUtf8();
	quint16 bits = 0;
	for (auto i = 0; i != kPermissionCount; ++i) {
		if (snap.permissions[i]) {
			bits |= (quint16(1) << i);
		}
	}
	stream << bits;
	stream << quint32(static_cast<uchar>(snap.backend));
	stream << quint32(snap.hiddenFolderNames.size());
	for (const auto &name : snap.hiddenFolderNames) {
		stream << name.toUtf8();
	}
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
		|| (version != kVersionV1 && version != kVersionV2)) {
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

	if (version >= kVersionV2) {
		quint32 nameCount = 0;
		stream >> nameCount;
		if (stream.status() != QDataStream::Ok) {
			return std::nullopt;
		}
		// Sanity cap so a corrupted file can't allocate gigabytes.
		constexpr quint32 kMaxNames = 1024u;
		if (nameCount > kMaxNames) {
			return std::nullopt;
		}
		snap.hiddenFolderNames.reserve(int(nameCount));
		for (quint32 i = 0; i != nameCount; ++i) {
			QByteArray nameBytes;
			stream >> nameBytes;
			if (stream.status() != QDataStream::Ok) {
				return std::nullopt;
			}
			snap.hiddenFolderNames.push_back(QString::fromUtf8(nameBytes));
		}
	}
	return snap;
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
