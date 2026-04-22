# Employee Permissions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parse + persist `token` and `permissions` from the employee login response, expose a typed query API (`Employee::Permissions`) on `Main::Account`, and refresh via `GET /api/auth/verify` on cold start.

**Architecture:** Introduce a dedicated `Intro::Employee::Permissions` class owned by `Main::Account`. Persist `{ token, permissions, backend }` to a new encrypted `FileKey` in `Storage::Account`, read on `Account::start()`, refresh asynchronously via `Intro::Employee::VerifyClient`. 401 from verify (or a corrupted/missing disk file with live MTP) triggers the existing `Account::forcedLogOut()`.

**Tech Stack:** C++20, Qt 6.11, rpl (tdesktop reactive library), QNetworkAccessManager (HTTP), `Storage::Account` encrypted file helpers (`FileWriteDescriptor` / `ReadEncryptedFile`), base::flat_map, tdesktop `LOG`.

**Scope:** Spec `docs/superpowers/specs/2026-04-22-employee-permissions-design.md`. Everything is gated by `#ifdef TDESKTOP_EMPLOYEE_MODE`, matching the existing employee login system.

**Handoff reminder:** Before asking the user to compile, each task ends with a short Chinese bullet summary of what changed.

**Testing approach:** tdesktop has no in-tree unit test harness. Correctness is verified by (a) compile after each task and (b) a 13-scenario manual integration matrix at the end (§9.2 of the spec). If a task introduces a pure function easy to exercise at runtime (e.g. `SerializeEmployeeAuth`/`Deserialize`), a diagnostic `LOG` line is added and removed in the cleanup task.

---

## File Map

### New files (6)

| Path | Purpose |
|---|---|
| `Telegram/SourceFiles/intro/employee/employee_permissions.h` | `enum class PermissionKey` (14 values), `class Permissions`, `JsonKeyToPermission()` map declaration |
| `Telegram/SourceFiles/intro/employee/employee_permissions.cpp` | `Permissions` implementation (rpl::variable-backed), static string→enum map |
| `Telegram/SourceFiles/intro/employee/employee_verify.h` | `VerifySuccess`/`VerifyFailure` structs, `VerifyClient` (QNetworkAccessManager wrapper) |
| `Telegram/SourceFiles/intro/employee/employee_verify.cpp` | `VerifyClient` implementation, 401 routing |
| `Telegram/SourceFiles/intro/employee/employee_auth_storage.h` | `AuthSnapshot` struct + `SerializeAuthSnapshot`/`DeserializeAuthSnapshot` free functions |
| `Telegram/SourceFiles/intro/employee/employee_auth_storage.cpp` | Binary serialization with magic + version |

### Modified files (6)

| Path | Change |
|---|---|
| `Telegram/CMakeLists.txt` | Register the 6 new source files |
| `Telegram/SourceFiles/intro/employee/employee_auth.h` | `AuthSuccess` adds `QString token` + `std::array<bool, 14> permissions` |
| `Telegram/SourceFiles/intro/employee/employee_auth.cpp` | `ParseAuthResponse` extracts token and permissions object |
| `Telegram/SourceFiles/intro/employee/employee_login_step.h/.cpp` | Pass new fields from `AuthSuccess` into `applyEmployeeBootstrap` |
| `Telegram/SourceFiles/main/main_account.h` | Extend `applyEmployeeBootstrap` signature; add `employeePermissions()`; new private members |
| `Telegram/SourceFiles/main/main_account.cpp` | Bootstrap writes snapshot; `start()` reads + kicks verify; `applyEmployeeReset` clears disk; verify callback |
| `Telegram/SourceFiles/storage/storage_account.h/.cpp` | New `lskEmployeeAuth` enum, `_employeeAuthKey` field, `writeEmployeeAuth`/`readEmployeeAuth`/`clearEmployeeAuth`, map read/write plumbing, `reset()` extension |

---

### Task 1: Create `Employee::Permissions` value type

**Files:**
- Create: `Telegram/SourceFiles/intro/employee/employee_permissions.h`
- Create: `Telegram/SourceFiles/intro/employee/employee_permissions.cpp`

- [ ] **Step 1: Write header**

`Telegram/SourceFiles/intro/employee/employee_permissions.h`:
```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/flat_map.h"

#include <array>

namespace Intro::Employee {

enum class PermissionKey : int {
	MsgSend = 0,
	MsgEdit,
	MsgDelete,
	MsgForward,
	GroupCreate,
	GroupDelete,
	GroupAddMember,
	GroupRemoveMember,
	ContactAdd,
	ContactBlock,
	ContactEditNote,
	FolderEdit,
	FolderAddChat,
	UiDisableMentionTooltip,
};
constexpr int kPermissionCount = 14;

using PermissionValues = std::array<bool, kPermissionCount>;

class Permissions final {
public:
	Permissions();
	~Permissions();

	[[nodiscard]] bool has(PermissionKey key) const;
	[[nodiscard]] rpl::producer<bool> value(PermissionKey key) const;
	[[nodiscard]] rpl::producer<> changes() const;
	[[nodiscard]] const QString &token() const;
	[[nodiscard]] bool authorized() const;

	void apply(PermissionValues values, QString token);
	void clear();

private:
	rpl::variable<PermissionValues> _values;
	QString _token;
};

// Backend JSON key (e.g. "msg.send") -> PermissionKey.
// Lookup: JsonKeyToPermission().find(key); missing = unknown, dropped by parser.
[[nodiscard]] const base::flat_map<QString, PermissionKey>
	&JsonKeyToPermission();

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 2: Write implementation**

`Telegram/SourceFiles/intro/employee/employee_permissions.cpp`:
```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_permissions.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

namespace Intro::Employee {

Permissions::Permissions() : _values(PermissionValues{}) {
}

Permissions::~Permissions() = default;

bool Permissions::has(PermissionKey key) const {
	const auto index = static_cast<int>(key);
	Expects(index >= 0 && index < kPermissionCount);
	return _values.current()[index];
}

rpl::producer<bool> Permissions::value(PermissionKey key) const {
	const auto index = static_cast<int>(key);
	Expects(index >= 0 && index < kPermissionCount);
	return _values.value() | rpl::map([=](const PermissionValues &v) {
		return v[index];
	}) | rpl::distinct_until_changed();
}

rpl::producer<> Permissions::changes() const {
	return _values.changes() | rpl::to_empty;
}

const QString &Permissions::token() const {
	return _token;
}

bool Permissions::authorized() const {
	return !_token.isEmpty();
}

void Permissions::apply(PermissionValues values, QString token) {
	_token = std::move(token);
	_values = values;
}

void Permissions::clear() {
	_token = QString();
	_values = PermissionValues{};
}

const base::flat_map<QString, PermissionKey> &JsonKeyToPermission() {
	static const auto kMap = base::flat_map<QString, PermissionKey>{
		{ u"msg.send"_q,                   PermissionKey::MsgSend },
		{ u"msg.edit"_q,                   PermissionKey::MsgEdit },
		{ u"msg.delete"_q,                 PermissionKey::MsgDelete },
		{ u"msg.forward"_q,                PermissionKey::MsgForward },
		{ u"group.create"_q,               PermissionKey::GroupCreate },
		{ u"group.delete"_q,               PermissionKey::GroupDelete },
		{ u"group.addMember"_q,            PermissionKey::GroupAddMember },
		{ u"group.removeMember"_q,         PermissionKey::GroupRemoveMember },
		{ u"contact.add"_q,                PermissionKey::ContactAdd },
		{ u"contact.block"_q,              PermissionKey::ContactBlock },
		{ u"contact.editNote"_q,           PermissionKey::ContactEditNote },
		{ u"folder.edit"_q,                PermissionKey::FolderEdit },
		{ u"folder.addChat"_q,             PermissionKey::FolderAddChat },
		{ u"ui.disableMentionTooltip"_q,   PermissionKey::UiDisableMentionTooltip },
	};
	return kMap;
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 3: Register in CMakeLists**

Modify `Telegram/CMakeLists.txt` — in the alphabetical `intro/employee/` block (currently at lines 1249–1256), insert these lines (keep alphabetical):
```
    intro/employee/employee_permissions.cpp
    intro/employee/employee_permissions.h
```

- [ ] **Step 4: Chinese compile summary**

```
- 新增 intro/employee/employee_permissions.{h,cpp}：PermissionKey 枚举（14 个）+ Permissions 类（rpl::variable 背书）+ 字符串到枚举的映射表
- CMakeLists.txt 添加这两个文件
```

- [ ] **Step 5: Commit**

```bash
git add Telegram/SourceFiles/intro/employee/employee_permissions.h \
        Telegram/SourceFiles/intro/employee/employee_permissions.cpp \
        Telegram/CMakeLists.txt
git commit -m "feat(employee): add Permissions value type + JSON key map"
```

---

### Task 2: Create `AuthSnapshot` serialization helpers

**Files:**
- Create: `Telegram/SourceFiles/intro/employee/employee_auth_storage.h`
- Create: `Telegram/SourceFiles/intro/employee/employee_auth_storage.cpp`

- [ ] **Step 1: Write header**

`Telegram/SourceFiles/intro/employee/employee_auth_storage.h`:
```cpp
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

namespace Intro::Employee {

struct AuthSnapshot {
	QString token;
	PermissionValues permissions{};
	BackendType backend = BackendType::Customer;
};

// Binary format (version 1):
//   u32 magic   = 'EMPA'    ('E'<<24|'M'<<16|'P'<<8|'A')
//   u32 version = 1
//   u32 tokenLen
//   u8[tokenLen] tokenUtf8
//   u16 permissionBits      (bit N = permissions[N])
//   u32 backend             (BackendType enum value, clamped on read)
[[nodiscard]] QByteArray SerializeAuthSnapshot(const AuthSnapshot &snap);
[[nodiscard]] std::optional<AuthSnapshot> DeserializeAuthSnapshot(
	const QByteArray &bytes);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 2: Write implementation**

`Telegram/SourceFiles/intro/employee/employee_auth_storage.cpp`:
```cpp
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
```

- [ ] **Step 3: Register in CMakeLists**

Insert in alphabetical order in `Telegram/CMakeLists.txt` intro/employee block:
```
    intro/employee/employee_auth_storage.cpp
    intro/employee/employee_auth_storage.h
```

- [ ] **Step 4: Chinese compile summary**

```
- 新增 intro/employee/employee_auth_storage.{h,cpp}：AuthSnapshot 结构 + 二进制序列化/反序列化
- 格式：magic 'EMPA' + version=1 + token + 14 位 permissionBits + backend
- 反序列化对坏 magic/未知版本/损坏数据返回 nullopt；越界 backend 降级为 Customer
- CMakeLists.txt 添加这两个文件
```

- [ ] **Step 5: Commit**

```bash
git add Telegram/SourceFiles/intro/employee/employee_auth_storage.h \
        Telegram/SourceFiles/intro/employee/employee_auth_storage.cpp \
        Telegram/CMakeLists.txt
git commit -m "feat(employee): add AuthSnapshot serialization helpers"
```

---

### Task 3: Add encrypted file key to `Storage::Account`

**Files:**
- Modify: `Telegram/SourceFiles/storage/storage_account.h`
- Modify: `Telegram/SourceFiles/storage/storage_account.cpp`

- [ ] **Step 1: Add `lskEmployeeAuth` to the enum**

In `storage_account.cpp`, locate the `// Local Storage Keys` enum (around line 74). The last entry is `lskPrefs = 0x1e`. Add immediately after it **inside `#ifdef TDESKTOP_EMPLOYEE_MODE`** so upstream merges stay clean:

```cpp
	lskPrefs = 0x1e, // no data
#ifdef TDESKTOP_EMPLOYEE_MODE
	lskEmployeeAuth = 0x1f, // no data
#endif
};
```

- [ ] **Step 2: Add `_employeeAuthKey` field + method declarations to `storage_account.h`**

Near the other `FileKey _xxxKey = 0;` declarations (around line 358), add:
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	FileKey _employeeAuthKey = 0;
#endif
```

In the public section (right after `writeMtpConfig();` around line 88), add:
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	void writeEmployeeAuth(const QByteArray &bytes);
	[[nodiscard]] QByteArray readEmployeeAuth();
	void clearEmployeeAuth();
#endif
```

- [ ] **Step 3: Implement write/read/clear in `storage_account.cpp`**

Add these three methods after `Account::writeLocations` / `Account::readLocations` (anywhere nearby, ~line 945). Wrap the whole block in `#ifdef TDESKTOP_EMPLOYEE_MODE` / `#endif`:

```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
void Account::writeEmployeeAuth(const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		clearEmployeeAuth();
		return;
	}
	if (!_employeeAuthKey) {
		_employeeAuthKey = GenerateKey(_basePath);
		writeMapQueued();
	}
	EncryptedDescriptor data(Serialize::bytearraySize(bytes));
	data.stream << bytes;
	FileWriteDescriptor file(_employeeAuthKey, _basePath);
	file.writeEncrypted(data, _localKey);
}

QByteArray Account::readEmployeeAuth() {
	if (!_employeeAuthKey) {
		return QByteArray();
	}
	FileReadDescriptor descriptor;
	if (!ReadEncryptedFile(
			descriptor, _employeeAuthKey, _basePath, _localKey)) {
		ClearKey(_employeeAuthKey, _basePath);
		_employeeAuthKey = 0;
		writeMapDelayed();
		return QByteArray();
	}
	QByteArray bytes;
	descriptor.stream >> bytes;
	if (!CheckStreamStatus(descriptor.stream)) {
		return QByteArray();
	}
	return bytes;
}

void Account::clearEmployeeAuth() {
	if (_employeeAuthKey) {
		ClearKey(_employeeAuthKey, _basePath);
		_employeeAuthKey = 0;
		writeMapDelayed();
	}
}
#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 4: Plumb into `writeMap()`**

In `writeMap()` (around line 596), find the `mapSize` accumulation block (lines 636–670). Add right before `EncryptedDescriptor mapData(mapSize);` (around line 668):
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	if (_employeeAuthKey) mapSize += sizeof(quint32) + sizeof(quint64);
#endif
```

In the serialization block further down (around line 705 after `_mediaLastPlaybackPositionsKey` / `_botStoragesMap` serialization), add:
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	if (_employeeAuthKey) {
		mapData.stream << quint32(lskEmployeeAuth) << quint64(_employeeAuthKey);
	}
#endif
```

- [ ] **Step 5: Plumb into `readMapWith()`**

Find the big `switch (blockId)` in `readMapWith()` (around line 375). Locate the last `case lsk...` block (the `default:` is nearby). Before `default:`, insert:
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
		case lskEmployeeAuth: {
			map.stream >> _employeeAuthKey;
		} break;
#endif
```

- [ ] **Step 6: Extend `Account::reset()` to zero the key**

In `Account::reset()` (around line 764), add near the other `_xxxKey = 0;` statements:
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	_employeeAuthKey = 0;
#endif
```

Note: `reset()` only clears the in-memory map. The actual file is removed by `clearEmployeeAuth()` which is called from `applyEmployeeReset()` in Task 9. `reset()` runs during full logout after `loggedOut()` → `local().reset()`, but at that point the file was already cleared via `clearEmployeeAuth()`.

- [ ] **Step 7: Chinese compile summary**

```
- storage_account：新增 lskEmployeeAuth=0x1f 枚举，_employeeAuthKey 成员
- 新增 writeEmployeeAuth / readEmployeeAuth / clearEmployeeAuth 三个方法（走既有 localKey 加密路径）
- writeMap/readMapWith 序列化新键；reset() 清零字段
- 全部在 #ifdef TDESKTOP_EMPLOYEE_MODE 内
```

- [ ] **Step 8: Compile checkpoint**

Ask the user to build. Expected: clean compile, no changes in runtime behavior yet (no caller references the new methods).

- [ ] **Step 9: Commit**

```bash
git add Telegram/SourceFiles/storage/storage_account.h \
        Telegram/SourceFiles/storage/storage_account.cpp
git commit -m "feat(employee): add encrypted employeeAuth fileKey to Storage::Account"
```

---

### Task 4: Extend `AuthSuccess` + `ParseAuthResponse` for token & permissions

**Files:**
- Modify: `Telegram/SourceFiles/intro/employee/employee_auth.h`
- Modify: `Telegram/SourceFiles/intro/employee/employee_auth.cpp`

- [ ] **Step 1: Extend `AuthSuccess`**

In `employee_auth.h`, add the permissions include and extend the struct:
```cpp
#include "intro/employee/employee_config.h"
#include "intro/employee/employee_permissions.h"
```

Change `AuthSuccess`:
```cpp
struct AuthSuccess {
	MTP::DcId dcId = 0;
	QByteArray authKey;   // 256 bytes, already hex-decoded
	UserId userId = 0;
	QString token;                // Bearer token for /api/auth/verify
	PermissionValues permissions{}; // backend-declared permission bits
};
```

- [ ] **Step 2: Extract token and permissions in `ParseAuthResponse`**

In `employee_auth.cpp`, locate the 2xx success branch (after the `kAuthKeyHexChars` check, roughly lines 125–134). Extend the `AuthSuccess` construction:

Before the current `return AuthSuccess{...}` block, add token and permissions extraction:
```cpp
	// token is required; without it we can't verify later.
	const auto token = root.value(u"token"_q).toString();
	if (token.isEmpty()) {
		LOG(("Employee: login response missing token"));
		return MakeFailure(
			AuthFailure::Kind::BadJson,
			tr::lng_employee_err_bad_json(tr::now));
	}

	// permissions is optional; missing/wrong type → all false (with log).
	auto permissions = PermissionValues{};
	const auto permsValue = root.value(u"permissions"_q);
	if (permsValue.isObject()) {
		const auto permsObj = permsValue.toObject();
		const auto &mapping = JsonKeyToPermission();
		for (auto it = permsObj.begin(); it != permsObj.end(); ++it) {
			const auto known = mapping.find(it.key());
			if (known == mapping.end()) {
				LOG(("Employee: unknown permission key: %1").arg(it.key()));
				continue;
			}
			const auto rawValue = it.value();
			if (!rawValue.isBool()) {
				LOG(("Employee: permission %1 is not a bool").arg(it.key()));
				permissions[static_cast<int>(known->second)] = false;
				continue;
			}
			permissions[static_cast<int>(known->second)] = rawValue.toBool();
		}
	} else if (permsValue.isUndefined() || permsValue.isNull()) {
		LOG(("Employee: login response has no permissions field"));
	} else {
		LOG(("Employee: login response permissions field is not an object"));
	}

	const auto userId = UserId(userIdRaw.toVariant().toLongLong());
	if (!userId.bare) {
		return MakeBadJson();
	}

	return AuthSuccess{
		MTP::DcId(dcId),
		keyBytes,
		userId,
		token,
		permissions,
	};
```

Delete the original `const auto userId = ...` / `return AuthSuccess{...}` block that this replaces (previously at lines 124–133).

- [ ] **Step 3: Chinese compile summary**

```
- employee_auth.h：AuthSuccess 增加 token 和 permissions 字段
- employee_auth.cpp：ParseAuthResponse 提取 token（缺失 → BadJson）和 permissions（对象 → 14 bool，未知 key 记日志丢弃，非对象 → 全 false）
```

- [ ] **Step 4: Compile checkpoint**

Ask the user to build. Expected: clean compile. Runtime: new fields populated but no consumer yet — next tasks wire them in.

- [ ] **Step 5: Commit**

```bash
git add Telegram/SourceFiles/intro/employee/employee_auth.h \
        Telegram/SourceFiles/intro/employee/employee_auth.cpp
git commit -m "feat(employee): parse token and permissions from login response"
```

---

### Task 5: Wire `Permissions` into `Main::Account`

**Files:**
- Modify: `Telegram/SourceFiles/main/main_account.h`
- Modify: `Telegram/SourceFiles/main/main_account.cpp`

- [ ] **Step 1: Extend `applyEmployeeBootstrap` signature + add member**

In `main_account.h`, update the `#ifdef TDESKTOP_EMPLOYEE_MODE` block (lines 51–57):
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	void applyEmployeeBootstrap(
		MTP::DcId dcId,
		std::shared_ptr<MTP::AuthKey> key,
		UserId userId,
		QString token,
		Intro::Employee::PermissionValues permissions,
		Intro::Employee::BackendType backend);
	void applyEmployeeReset();

	[[nodiscard]] const Intro::Employee::Permissions &
		employeePermissions() const;
#endif
```

In the private members section (after `_loggingOut`, around line 175), add:
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	std::unique_ptr<Intro::Employee::Permissions> _employeePermissions;
	Intro::Employee::BackendType _employeeBackend =
		Intro::Employee::BackendType::Customer;
#endif
```

Include headers at the top of `main_account.h` (alongside the existing `mtproto/` includes):
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
#include "intro/employee/employee_config.h"
#include "intro/employee/employee_permissions.h"
#endif
```

- [ ] **Step 2: Construct `_employeePermissions` in Account's constructor**

In `main_account.cpp`, find the `Account::Account(...)` constructor (around line 72–90). At the end of its member-initializer list (or in the body), initialize `_employeePermissions` inside `#ifdef TDESKTOP_EMPLOYEE_MODE`. Easiest: add in the constructor body:
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	_employeePermissions =
		std::make_unique<Intro::Employee::Permissions>();
#endif
```

- [ ] **Step 3: Add the accessor and update bootstrap**

Replace the existing `applyEmployeeBootstrap` / `applyEmployeeReset` in `main_account.cpp` (currently around lines 650–703) with:
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
const Intro::Employee::Permissions &Account::employeePermissions() const {
	return *_employeePermissions;
}

void Account::applyEmployeeBootstrap(
		MTP::DcId dcId,
		std::shared_ptr<MTP::AuthKey> key,
		UserId userId,
		QString token,
		Intro::Employee::PermissionValues permissions,
		Intro::Employee::BackendType backend) {
	Expects(key != nullptr);
	Expects(dcId > 0);
	Expects(userId.bare != 0);

	LOG(("Employee: bootstrap dcId=%1 userId=%2 tokenLen=%3 backend=%4"
		).arg(dcId
		).arg(userId.bare
		).arg(token.size()
		).arg(static_cast<uchar>(backend)));

	_employeeBackend = backend;
	_employeePermissions->apply(permissions, token);
	local().writeEmployeeAuth(Intro::Employee::SerializeAuthSnapshot(
		Intro::Employee::AuthSnapshot{
			.token = token,
			.permissions = permissions,
			.backend = backend,
		}));

	_mtpFields.mainDcId = dcId;
	_mtpFields.keys = { key };
	(void)userId;

	{
		const auto old = _mtp ? base::take(_mtp) : nullptr;
		LOG(("Employee: bootstrap step=drop_old_mtp"));
		auto config = std::make_unique<MTP::Config>(
			Core::App().fallbackProductionConfig());
		LOG(("Employee: bootstrap step=config_ready"));
		startMtp(std::move(config));
		LOG(("Employee: bootstrap step=startMtp_returned"));
	}
	LOG(("Employee: bootstrap step=old_mtp_destroyed"));
}

void Account::applyEmployeeReset() {
	if (_mtp) {
		base::take(_mtp);
	}
	_mtpFields.keys.clear();
	_mtpFields.mainDcId = MTP::Instance::Fields::kNoneMainDc;
	_sessionUserId = {};
	_employeePermissions->clear();
	_employeeBackend = Intro::Employee::BackendType::Customer;
	local().clearEmployeeAuth();

	LOG(("Employee: reset"));
}
#endif // TDESKTOP_EMPLOYEE_MODE
```

Include `employee_auth_storage.h` near the top of `main_account.cpp`:
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
#include "intro/employee/employee_auth_storage.h"
#endif
```

- [ ] **Step 4: Update the login step to pass new arguments**

In `Telegram/SourceFiles/intro/employee/employee_login_step.cpp`, find `onLoginSuccess` (or whichever function calls `applyEmployeeBootstrap`). The existing call is currently:
```cpp
account().applyEmployeeBootstrap(result.dcId, key, result.userId);
```

Replace with:
```cpp
account().applyEmployeeBootstrap(
	result.dcId,
	key,
	result.userId,
	result.token,
	result.permissions,
	_chosenBackend);
```

(`_chosenBackend` already exists as a member of `EmployeeLoginStep` — confirmed at `employee_login_step.h:62`.)

- [ ] **Step 5: Chinese compile summary**

```
- main_account.h：applyEmployeeBootstrap 签名加三个参数（token/permissions/backend）；新增 employeePermissions() 访问器、_employeePermissions/_employeeBackend 成员
- main_account.cpp：构造函数 new Permissions；bootstrap 写盘 + apply；reset 清类 + 清盘
- employee_login_step.cpp：调用点传入 token/permissions/chosenBackend
```

- [ ] **Step 6: Compile checkpoint**

Ask the user to build and **smoke-test login**. Expected: login flow unchanged from user's perspective; new LOG shows `bootstrap ... tokenLen=N backend=X`; the encrypted auth file appears under the account folder.

- [ ] **Step 7: Commit**

```bash
git add Telegram/SourceFiles/main/main_account.h \
        Telegram/SourceFiles/main/main_account.cpp \
        Telegram/SourceFiles/intro/employee/employee_login_step.cpp
git commit -m "feat(employee): persist permissions+token in applyEmployeeBootstrap"
```

---

### Task 6: Read snapshot on cold start and populate `Permissions`

**Files:**
- Modify: `Telegram/SourceFiles/main/main_account.cpp`

- [ ] **Step 1: Find `Account::start` tail**

Locate `void Account::start(std::unique_ptr<MTP::Config> config)` in `main_account.cpp`. It currently ends by calling `startMtp(std::move(config))` and then some post-init. We want to read the snapshot **before** `startMtp` so that the in-memory `_employeePermissions` is populated by the time any subscriber could look at it.

- [ ] **Step 2: Add snapshot read + apply**

In `Account::start()`, add at the very beginning of the method body (before any existing logic that touches `_mtp`):
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	{
		const auto bytes = local().readEmployeeAuth();
		if (!bytes.isEmpty()) {
			if (auto snap = Intro::Employee::DeserializeAuthSnapshot(bytes)) {
				LOG(("Employee: cold start loaded snapshot tokenLen=%1 backend=%2"
					).arg(snap->token.size()
					).arg(static_cast<uchar>(snap->backend)));
				_employeePermissions->apply(snap->permissions, snap->token);
				_employeeBackend = snap->backend;
			} else {
				LOG(("Employee: cold start snapshot corrupted; will force logout"));
				// Defer: we can't forcedLogOut() here because Session isn't
				// built yet. Leave _employeePermissions unauthorized — Task 8
				// detects "MTP present but not authorized" and triggers hard
				// logout on the event loop.
			}
		}
	}
#endif
```

Note: actual hard-logout wiring for the "corrupted snapshot" / "empty snapshot with MTP present" case is added in **Task 8** alongside the verify kickoff logic. At this stage, a corrupted snapshot simply leaves `_employeePermissions` empty; the Task 8 logic will detect that state and trigger logout.

- [ ] **Step 3: Chinese compile summary**

```
- main_account.cpp：Account::start() 开头读 employeeAuth 文件 → 反序列化 → apply()
- 反序列化失败只打日志，由 Task 8 的 verify kickoff 统一触发硬登出
```

- [ ] **Step 4: Compile checkpoint**

Ask the user to build, login once (to produce the snapshot), close, re-open. Expected new LOG: `cold start loaded snapshot tokenLen=N backend=0`.

- [ ] **Step 5: Commit**

```bash
git add Telegram/SourceFiles/main/main_account.cpp
git commit -m "feat(employee): read permissions snapshot on cold start"
```

---

### Task 7: Implement `VerifyClient`

**Files:**
- Create: `Telegram/SourceFiles/intro/employee/employee_verify.h`
- Create: `Telegram/SourceFiles/intro/employee/employee_verify.cpp`

- [ ] **Step 1: Write header**

`Telegram/SourceFiles/intro/employee/employee_verify.h`:
```cpp
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

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <variant>

class QNetworkAccessManager;
class QNetworkReply;

namespace Intro::Employee {

struct VerifySuccess {
	PermissionValues permissions{};
};

struct VerifyFailure {
	enum class Kind { Network, Server, InvalidToken, BadJson };
	Kind kind = Kind::Network;
};

using VerifyResult = std::variant<VerifySuccess, VerifyFailure>;

class VerifyClient final : public QObject {
public:
	explicit VerifyClient(QObject *parent = nullptr);
	~VerifyClient();

	void verify(
		BackendType backend,
		QString token,
		Fn<void(VerifyResult)> done);
	void cancel();

private:
	std::unique_ptr<QNetworkAccessManager> _nam;
	QPointer<QNetworkReply> _reply;
};

[[nodiscard]] VerifyResult ParseVerifyResponse(
	int httpStatus,
	const QByteArray &body);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 2: Write implementation**

`Telegram/SourceFiles/intro/employee/employee_verify.cpp`:
```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_verify.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/debug_log.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Intro::Employee {

VerifyResult ParseVerifyResponse(int httpStatus, const QByteArray &body) {
	if (httpStatus == 401) {
		LOG(("Employee: verify 401 invalid token"));
		return VerifyFailure{ VerifyFailure::Kind::InvalidToken };
	}
	if (httpStatus >= 500) {
		LOG(("Employee: verify server error http=%1").arg(httpStatus));
		return VerifyFailure{ VerifyFailure::Kind::Server };
	}
	if (httpStatus >= 400) {
		LOG(("Employee: verify client error http=%1").arg(httpStatus));
		return VerifyFailure{ VerifyFailure::Kind::Server };
	}

	auto parseError = QJsonParseError{};
	const auto doc = QJsonDocument::fromJson(body, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		LOG(("Employee: verify bad json"));
		return VerifyFailure{ VerifyFailure::Kind::BadJson };
	}
	const auto root = doc.object();
	const auto permsValue = root.value(u"permissions"_q);
	if (!permsValue.isObject()) {
		LOG(("Employee: verify response has no permissions object"));
		return VerifyFailure{ VerifyFailure::Kind::BadJson };
	}

	auto permissions = PermissionValues{};
	const auto permsObj = permsValue.toObject();
	const auto &mapping = JsonKeyToPermission();
	for (auto it = permsObj.begin(); it != permsObj.end(); ++it) {
		const auto known = mapping.find(it.key());
		if (known == mapping.end()) {
			LOG(("Employee: verify unknown key: %1").arg(it.key()));
			continue;
		}
		const auto rawValue = it.value();
		if (rawValue.isBool()) {
			permissions[static_cast<int>(known->second)] = rawValue.toBool();
		}
	}
	return VerifySuccess{ permissions };
}

VerifyClient::VerifyClient(QObject *parent)
: QObject(parent)
, _nam(std::make_unique<QNetworkAccessManager>()) {
}

VerifyClient::~VerifyClient() {
	cancel();
}

void VerifyClient::verify(
		BackendType backend,
		QString token,
		Fn<void(VerifyResult)> done) {
	cancel();

	if (token.isEmpty()) {
		done(VerifyFailure{ VerifyFailure::Kind::InvalidToken });
		return;
	}

	const auto info = BackendInfoFor(backend);
	auto url = QUrl();
	url.setScheme(u"http"_q);
	url.setHost(info.host);
	url.setPort(info.port);
	url.setPath(u"/api/auth/verify"_q);

	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);
	request.setRawHeader(
		"Authorization",
		(u"Bearer "_q + token).toLatin1());

	_reply = _nam->get(request);
	const auto reply = _reply.data();
	const auto finishedCallback = [this, reply, done = std::move(done)]() {
		if (!reply || reply != _reply.data()) {
			return;
		}
		const auto status = reply
			->attribute(QNetworkRequest::HttpStatusCodeAttribute)
			.toInt();
		if (reply->error() != QNetworkReply::NoError && status == 0) {
			const auto err = int(reply->error());
			LOG(("Employee: verify network error code=%1").arg(err));
			reply->deleteLater();
			_reply.clear();
			done(VerifyFailure{ VerifyFailure::Kind::Network });
			return;
		}
		const auto body = reply->readAll();
		reply->deleteLater();
		_reply.clear();
		done(ParseVerifyResponse(status, body));
	};
	QObject::connect(
		reply,
		&QNetworkReply::finished,
		this,
		finishedCallback);
}

void VerifyClient::cancel() {
	if (_reply) {
		_reply->disconnect();
		_reply->abort();
		_reply->deleteLater();
		_reply.clear();
	}
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 3: Register in CMakeLists**

Insert alphabetically in `Telegram/CMakeLists.txt`:
```
    intro/employee/employee_verify.cpp
    intro/employee/employee_verify.h
```

- [ ] **Step 4: Chinese compile summary**

```
- 新增 intro/employee/employee_verify.{h,cpp}：VerifyClient 封装 QNetworkAccessManager
- GET /api/auth/verify + Authorization: Bearer $token
- ParseVerifyResponse：401 → InvalidToken；4xx/5xx → Server；JSON 坏 → BadJson；成功 → permissions
- 未知 key 记日志丢弃；非 bool 值当作 false
- CMakeLists.txt 添加两个文件
```

- [ ] **Step 5: Compile checkpoint**

Ask the user to build. Expected: clean compile; no runtime change yet (no caller).

- [ ] **Step 6: Commit**

```bash
git add Telegram/SourceFiles/intro/employee/employee_verify.h \
        Telegram/SourceFiles/intro/employee/employee_verify.cpp \
        Telegram/CMakeLists.txt
git commit -m "feat(employee): add VerifyClient for /api/auth/verify"
```

---

### Task 8: Kick off verify on cold start; handle all result variants

**Files:**
- Modify: `Telegram/SourceFiles/main/main_account.h`
- Modify: `Telegram/SourceFiles/main/main_account.cpp`

- [ ] **Step 1: Add verify member + helper methods to header**

In `main_account.h`, add to the forward declarations:
```cpp
namespace Intro::Employee {
class Permissions;
class VerifyClient;
struct VerifySuccess;
struct VerifyFailure;
} // namespace Intro::Employee
```

Inside the `#ifdef TDESKTOP_EMPLOYEE_MODE` private member section, add alongside `_employeePermissions`:
```cpp
std::unique_ptr<Intro::Employee::VerifyClient> _employeeVerify;
```

Add private helper declarations (still inside `#ifdef`):
```cpp
void kickOffEmployeeVerifyIfAuthorized();
void onEmployeeVerifyInvalidToken();
void handleEmployeeAuthMissingOrCorrupt();
```

- [ ] **Step 2: Construct `_employeeVerify` in Account's constructor**

In `main_account.cpp`, alongside the existing `_employeePermissions = ...` constructor body init (added in Task 5):
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	_employeePermissions =
		std::make_unique<Intro::Employee::Permissions>();
	_employeeVerify = std::make_unique<Intro::Employee::VerifyClient>();
#endif
```

Include the verify header at the top of `main_account.cpp`:
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
#include "intro/employee/employee_auth_storage.h"
#include "intro/employee/employee_verify.h"
#endif
```

- [ ] **Step 3: Extend `Account::start()` — use snapshot state to decide action**

Replace the snapshot-read block added in Task 6 so it distinguishes three cases. The new block (at the beginning of `Account::start()`):
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	{
		const auto bytes = local().readEmployeeAuth();
		if (bytes.isEmpty()) {
			LOG(("Employee: cold start no snapshot (first install or cleared)"));
			// If MTP is about to come up with an existing auth key but we
			// have no token, we must re-login. Defer the check to after
			// MTP start so _mtpFields reflects real state.
		} else if (auto snap = Intro::Employee::DeserializeAuthSnapshot(bytes)) {
			LOG(("Employee: cold start snapshot tokenLen=%1 backend=%2"
				).arg(snap->token.size()
				).arg(static_cast<uchar>(snap->backend)));
			_employeePermissions->apply(snap->permissions, snap->token);
			_employeeBackend = snap->backend;
		} else {
			LOG(("Employee: cold start snapshot corrupted"));
		}
	}
#endif
```

- [ ] **Step 4: Trigger verify / hard-logout decision at the end of `start()`**

Still in `Account::start()`, **after** everything else runs (at the very end of the function body), add:
```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	kickOffEmployeeVerifyIfAuthorized();
#endif
```

- [ ] **Step 5: Implement `kickOffEmployeeVerifyIfAuthorized` + handlers**

Append to `main_account.cpp`, inside the existing `#ifdef TDESKTOP_EMPLOYEE_MODE` block for employee code.

**State probe note.** By the time `kickOffEmployeeVerifyIfAuthorized()` runs at the end of `Account::start()`, `_mtpFields` has been moved from (by `startMtp`), so `_mtpFields.keys` is always empty here — do not use it as a probe. The reliable indicator that "this cold start has an authenticated account" is `maybeSession() != nullptr`: `startMtp` calls `createSession(UserId, serialized, ...)` if `_sessionUserId` was set by `prepareToStart` (i.e. saved MTP data on disk).

```cpp
void Account::kickOffEmployeeVerifyIfAuthorized() {
	// Fresh install / no saved account: Intro will handle login, nothing to do.
	if (!maybeSession()) {
		return;
	}

	// Saved account but no token => snapshot missing or corrupted.
	// Force re-login on the next event loop tick (cannot call forcedLogOut
	// directly while start() is still unwinding).
	if (!_employeePermissions->authorized()) {
		LOG(("Employee: session present but no token; scheduling forced logout"));
		crl::on_main(this, [this] {
			handleEmployeeAuthMissingOrCorrupt();
		});
		return;
	}

	LOG(("Employee: kicking off verify"));
	const auto token = _employeePermissions->token();
	_employeeVerify->verify(
		_employeeBackend,
		token,
		crl::guard(this, [this](Intro::Employee::VerifyResult result) {
			if (const auto s = std::get_if<
					Intro::Employee::VerifySuccess>(&result)) {
				LOG(("Employee: verify ok"));
				const auto token = _employeePermissions->token();
				_employeePermissions->apply(s->permissions, token);
				local().writeEmployeeAuth(
					Intro::Employee::SerializeAuthSnapshot(
						Intro::Employee::AuthSnapshot{
							.token = token,
							.permissions = s->permissions,
							.backend = _employeeBackend,
						}));
				return;
			}
			const auto f = std::get_if<
				Intro::Employee::VerifyFailure>(&result);
			Assert(f != nullptr);
			switch (f->kind) {
			case Intro::Employee::VerifyFailure::Kind::InvalidToken:
				onEmployeeVerifyInvalidToken();
				return;
			case Intro::Employee::VerifyFailure::Kind::Network:
			case Intro::Employee::VerifyFailure::Kind::Server:
			case Intro::Employee::VerifyFailure::Kind::BadJson:
				LOG(("Employee: verify failed kind=%1; keeping disk state"
					).arg(int(f->kind)));
				return;
			}
		}));
}

void Account::onEmployeeVerifyInvalidToken() {
	LOG(("Employee: verify 401 -> forced logout"));
	handleEmployeeAuthMissingOrCorrupt();
}

void Account::handleEmployeeAuthMissingOrCorrupt() {
	// Clear local employee state first so re-login starts from a clean slate,
	// then use the existing forcedLogOut path which tears down the session
	// and triggers the Main::Domain transition back to Intro.
	_employeePermissions->clear();
	local().clearEmployeeAuth();
	_employeeBackend = Intro::Employee::BackendType::Customer;
	forcedLogOut();
}
```

- [ ] **Step 6: Ensure `applyEmployeeReset` also cancels in-flight verify**

Update the `applyEmployeeReset` body added in Task 5:
```cpp
void Account::applyEmployeeReset() {
	if (_employeeVerify) {
		_employeeVerify->cancel();
	}
	if (_mtp) {
		base::take(_mtp);
	}
	_mtpFields.keys.clear();
	_mtpFields.mainDcId = MTP::Instance::Fields::kNoneMainDc;
	_sessionUserId = {};
	_employeePermissions->clear();
	_employeeBackend = Intro::Employee::BackendType::Customer;
	local().clearEmployeeAuth();

	LOG(("Employee: reset"));
}
```

Also in `applyEmployeeBootstrap` (Task 5), at the very start cancel any lingering verify:
```cpp
void Account::applyEmployeeBootstrap(...) {
	...
	_employeeVerify->cancel();  // ← NEW: cancel any in-flight verify first

	LOG(("Employee: bootstrap dcId=%1 ..."));
	...
}
```

- [ ] **Step 7: Chinese compile summary**

```
- main_account：Account::start() 结尾调 kickOffEmployeeVerifyIfAuthorized()
- 三分支：无 MTP + 无 token = 清爽新装，啥都不做；MTP 有但 token 无 = 硬登出（延后到下次事件循环）；都有 = verify
- verify 成功：apply + 重新写盘
- verify 401：硬登出（清类 + 清盘 + forcedLogOut）
- verify 网络/服务/JSON 错：保留磁盘不动，只打日志
- applyEmployeeReset + applyEmployeeBootstrap：取消任何正在跑的 verify
```

- [ ] **Step 8: Compile checkpoint — runs the real flow end-to-end**

Ask the user to build and test this matrix:
1. Fresh login → main window → close → reopen → check for `cold start snapshot tokenLen=...` + `kicking off verify` + `verify ok`
2. Backend shuts down `verify` endpoint / returns 401 → cold restart → check for `verify 401 -> forced logout` → user lands on Intro

- [ ] **Step 9: Commit**

```bash
git add Telegram/SourceFiles/main/main_account.h \
        Telegram/SourceFiles/main/main_account.cpp
git commit -m "feat(employee): verify on cold start + 401 hard logout"
```

---

### Task 9: Run the full manual integration matrix

**Files:** (none modified in this task)

- [ ] **Step 1: Run spec §9.2 matrix**

Walk through all 13 scenarios from the spec. Record pass/fail per scenario with the actual LOG lines seen.

| # | Scenario | Expected |
|---|---|---|
| 1 | New login, all perms true | `bootstrap tokenLen=... backend=...`; main window opens |
| 2 | Cold restart after (1) | `cold start snapshot tokenLen=...`; `kicking off verify`; `verify ok` |
| 3 | Backend flips `msg.send` → false between sessions, cold restart | Disk still true at launch; seconds later `verify ok`; disk rewritten |
| 4 | Backend revokes token, cold restart | `verify 401 -> forced logout`; Intro shown |
| 5 | Cold restart offline | `cold start snapshot ...`; `verify failed kind=0` (Network); disk preserved |
| 6 | Login with `permissions` field missing | `login response has no permissions field`; main window opens |
| 7 | Backend adds `msg.pin` (unknown) | `verify unknown key: msg.pin`; other perms unaffected |
| 8 | Manual logout | `Employee: reset`; file cleared; restart → Intro |
| 9 | Login, logout, login rapid | Second bootstrap cancels first verify; second verify uses fresh token |
| 10 | Logout during in-flight verify | Verify callback suppressed (reply disconnected) |
| 11 | Disk file corruption (manually edit bytes) | `cold start snapshot corrupted`; `scheduling forced logout`; Intro shown |
| 12 | Verify response has non-bool permission value | Value treated as false; other perms parsed |
| 13 | First cold start after feature ships (no file yet) | `cold start no snapshot`; `scheduling forced logout`; Intro shown |

- [ ] **Step 2: If any scenario fails**

File a follow-up task in `TaskCreate`; do NOT paper over failures by adjusting expectations. Root-cause, fix, re-run, commit the fix separately.

- [ ] **Step 3: If all scenarios pass, commit a test log**

Write the results to `docs/superpowers/test-logs/2026-04-22-employee-permissions.md` (short markdown table of #, scenario, observed, pass/fail).

```bash
git add docs/superpowers/test-logs/2026-04-22-employee-permissions.md
git commit -m "test(employee): permissions data layer manual matrix results"
```

---

### Task 10: Cleanup diagnostic LOGs

**Files:**
- Modify: `Telegram/SourceFiles/main/main_account.cpp`
- Modify: `Telegram/SourceFiles/intro/employee/employee_verify.cpp`
- Modify: `Telegram/SourceFiles/intro/employee/employee_auth.cpp`

- [ ] **Step 1: Identify keeper vs diagnostic LOGs**

Keepers (signal on real events, low volume):
- `bootstrap dcId=... userId=... tokenLen=... backend=...`
- `Employee: reset`
- `cold start snapshot tokenLen=... backend=...`
- `cold start no snapshot`
- `cold start snapshot corrupted`
- `kicking off verify`
- `verify ok`
- `verify 401 -> forced logout`
- `verify failed kind=...`
- `unknown permission key: ...`
- `unknown key: ...`
- `bad json`
- `network error code=...`

Removable (loop through step markers; only useful during crash triage):
- `bootstrap step=drop_old_mtp`
- `bootstrap step=config_ready`
- `bootstrap step=startMtp_returned`
- `bootstrap step=old_mtp_destroyed`
- `startMtp step=enter/fields_ready/instance_built/handlers_set/session_maybe_created`
- `authkey raw bytes ...` (if any still present from login debugging)

- [ ] **Step 2: Remove the step= LOGs**

Use Grep + Edit to strip the `step=` LOGs from `main_account.cpp` inside the employee-mode functions. Keep the `reset`, `bootstrap dcId=`, `verify` event LOGs.

- [ ] **Step 3: Chinese compile summary**

```
- 清理调试期 LOG（step=xxx、authkey raw bytes、startMtp step 系列）
- 保留有实际诊断价值的 LOG：bootstrap、reset、cold start、verify ok/fail、unknown key
```

- [ ] **Step 4: Compile checkpoint**

Ask the user to build. Expected: clean compile; LOG output notably quieter.

- [ ] **Step 5: Commit**

```bash
git add Telegram/SourceFiles/main/main_account.cpp \
        Telegram/SourceFiles/intro/employee/employee_verify.cpp \
        Telegram/SourceFiles/intro/employee/employee_auth.cpp
git commit -m "chore(employee): remove bootstrap diagnostic step= LOGs"
```

---

## Self-review checklist

### Spec coverage

- ✅ Goal 1 (parse + persist token/perms) → Tasks 4, 5
- ✅ Goal 2 (typed query API on Account) → Tasks 1, 5
- ✅ Goal 3 (rpl subscription) → Task 1 (`value()`, `changes()`)
- ✅ Goal 4 (cold start: disk first + async verify) → Tasks 6, 8
- ✅ Goal 5 (401 → hard logout to Intro) → Task 8 (`forcedLogOut`)
- ✅ Goal 6 (single encrypted file, Storage::Account) → Tasks 2, 3
- ✅ §7.5 hard logout via `forcedLogOut()` covers 401 and disk corruption → Task 8
- ✅ §7.4 concurrency (verify cancel on bootstrap/reset) → Task 8 Step 6
- ✅ §8 error handling (bad JSON, unknown keys, network) → Tasks 4 (parse), 7 (verify), 8 (recovery)
- ✅ §9.2 manual matrix → Task 9
- ✅ §11 migration risk for existing v2 users → Task 8 Step 5 branch `MTP present but no token`

### Type consistency

- `PermissionValues = std::array<bool, kPermissionCount>` used consistently in: `Permissions::apply`, `AuthSuccess`, `AuthSnapshot`, `VerifySuccess`, `applyEmployeeBootstrap`
- `BackendType` enum from `employee_config.h` used in: `AuthSnapshot`, `applyEmployeeBootstrap`, `_employeeBackend`, `VerifyClient::verify`
- `employeePermissions()` returns `const Permissions &` (ref, not pointer) — used in spec §6 and Task 5
- `JsonKeyToPermission()` returns `const base::flat_map<QString, PermissionKey> &` in both employee_auth.cpp and employee_verify.cpp — same underlying static map

### Placeholder scan

- No "TBD", "TODO", "implement later", "fill in details" present
- No "Add appropriate error handling" without concrete behavior — every error case names its handling (disk preserved, log, hard logout, etc.)
- Every step that edits code shows the exact code
- Every manual test in Task 9 has an expected LOG line pattern
