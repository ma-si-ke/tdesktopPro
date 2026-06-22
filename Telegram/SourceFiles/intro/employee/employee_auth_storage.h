/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_config.h"
#include "intro/employee/employee_permissions.h"

#include <optional>

#include <QtCore/QStringList>

namespace Intro::Employee {

struct AuthSnapshot {
	QString token;
	PermissionValues permissions{};
	BackendType backend = BackendType::Customer;
	QStringList hiddenFolderNames;
	QString openTime; // v3: "HH:MM-HH:MM" availability window; empty = always.
};

// Binary format:
//   u32 magic   = 'EMPA'    ('E'<<24|'M'<<16|'P'<<8|'A')
//   u32 version = 1 or 2
//   u32 tokenLen
//   u8[tokenLen] tokenUtf8
//   u16 permissionBits      (bit N = permissions[N])
//   u32 backend             (BackendType enum value, clamped on read)
//   --- v2 only ---
//   u32 hiddenFolderNamesCount
//   { u32 nameLen; u8[nameLen] nameUtf8 } * count
//
// Reader accepts both v1 (no folder list) and v2; writer always emits v2.
[[nodiscard]] QByteArray SerializeAuthSnapshot(const AuthSnapshot &snap);
[[nodiscard]] std::optional<AuthSnapshot> DeserializeAuthSnapshot(
	const QByteArray &bytes);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
