# Employee Login Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the default phone/SMS Intro entry point with a company-backend HTTP login that converges with tdesktop's existing session-creation code path at `Intro::details::Step::finish(user)`.

**Architecture:** A new `Intro::Employee::EmployeeLoginStep` (subclass of `Intro::details::Step`) posts `{username, password}` to a company HTTP backend, receives `{dcId, authKeyHex, userId}`, injects the key into `Main::Account` via a new additive method `applyEmployeeBootstrap`, waits for MTP to connect, issues `users.GetUsers([InputUserSelf])` to fetch the real `MTPUser`, then calls inherited `Step::finish(user)` — the same method the SMS flow uses. All changes are gated behind `TDESKTOP_EMPLOYEE_MODE`.

**Tech Stack:** C++20, Qt 6.11, MSVC VS 2026, tdesktop's rpl/MTP/Intro frameworks. Build via `win.bat qt6` (incremental builds thereafter from the VS solution).

**Testing note:** tdesktop does not use a unit-test framework for features at this level. Verification is manual via the checklist in §9 of the spec plus log inspection. A few small self-tests are written as free functions that get exercised from a temporary debug log statement at startup (removed before final commit).

**Spec reference:** `docs/superpowers/specs/2026-04-21-employee-login-redesign.md`. Read it before starting. Every task below references sections of that spec.

---

## File Structure

### New files (under `Telegram/SourceFiles/intro/employee/`)

| File | Responsibility |
|---|---|
| `employee_config.h` / `.cpp` | `BackendType` enum, `BackendInfo` struct, 4 backend constants, `BackendInfoFor` / `AllBackends`. Pure data. |
| `employee_auth.h` / `.cpp` | `AuthClient` class (async HTTP wrapper) + free function `ParseAuthResponse(QByteArray) -> AuthResult` (pure, testable). |
| `employee_prefs.h` / `.cpp` | Two getters/setters backed by `Core::App().settings()`: last backend + last username. |
| `employee_login_step.h` / `.cpp` | `EmployeeLoginStep : Intro::details::Step`. UI layout + bootstrap orchestration. |

### Modified files

| File | Change |
|---|---|
| `Telegram/CMakeLists.txt` | (a) Define `TDESKTOP_EMPLOYEE_MODE`. (b) Register the 4 new files. (c) Add `Dnsapi` link + `/DELAYLOAD:dnsapi.dll` (pre-emptive — known LNK2019 from prior attempt). |
| `Telegram/SourceFiles/main/main_account.h` | Add public methods `applyEmployeeBootstrap` and `applyEmployeeReset`, both `#ifdef`-gated. |
| `Telegram/SourceFiles/main/main_account.cpp` | Implement the two methods. No other changes. |
| `Telegram/SourceFiles/intro/intro_widget.cpp` | In the `EnterPoint` switch (lines ~106–118), when `TDESKTOP_EMPLOYEE_MODE` is defined route every case to `EmployeeLoginStep`. |
| `Telegram/Resources/langs/lang_en.strings` | Add 14 `lng_employee_*` keys (values per spec §8). |
| `Telegram/Resources/langs/lang.strings` (zh-hans variant if present) | Same keys with translated values. (Skip if project uses only `lang_en.strings`; see Task 3.) |

### Commit discipline

One commit per task. Every task ends with a clean build (or documents the expected build status if intermediate).

---

## Task 1 — Add compile flag, Dnsapi link, and register empty new-files placeholder

**Goal:** Stand up the scaffolding so later tasks can assume the flag exists and new files can be added to the build without re-editing CMake each time. Verify upstream still compiles.

**Files:**
- Modify: `Telegram/CMakeLists.txt`

**Steps:**

- [ ] **Step 1.1: Read the existing CMakeLists to find the right insertion points.**

Read: `Telegram/CMakeLists.txt`.
Find two anchors:
1. The `target_compile_definitions(Telegram PRIVATE ...)` block for existing flags.
2. The `target_link_libraries(Telegram PRIVATE ...)` block where Windows-specific libs like `iphlpapi` are listed.
3. The `nice_target_sources(Telegram ${src_loc} PRIVATE ...)` block listing `intro/intro_widget.cpp` and siblings.

Capture the exact line numbers for use below. (Each CMakeLists differs slightly; do not copy line numbers from this plan without re-checking.)

- [ ] **Step 1.2: Add the compile definition.**

Inside the existing `target_compile_definitions(Telegram PRIVATE ...)` block, append:

```cmake
TDESKTOP_EMPLOYEE_MODE=1
```

If there's no existing `target_compile_definitions` block for `Telegram`, add one right after `target_include_directories(Telegram ...)`:

```cmake
target_compile_definitions(Telegram
PRIVATE
    TDESKTOP_EMPLOYEE_MODE=1
)
```

- [ ] **Step 1.3: Add Dnsapi + delay-load on Windows.**

Find the `if (WIN32)` block in `Telegram/CMakeLists.txt`. In the `target_link_libraries(Telegram PRIVATE ...)` list of Windows libs, add `Dnsapi`. In the linker flags section (look for `/DELAYLOAD:iphlpapi.dll` or similar), append `/DELAYLOAD:dnsapi.dll`.

Concrete example of the libs line:
```cmake
target_link_libraries(Telegram
PRIVATE
    Dnsapi
    # ... existing libs stay as-is
)
```

For the delay-load line (look for the existing `target_link_options` with `/DELAYLOAD:` entries):
```cmake
/DELAYLOAD:dnsapi.dll
```

Rationale note for the engineer: prior build attempts without this produced `LNK2019: unresolved external symbol DnsQueryEx` from `Qt6Networkd.lib`. Adding the lib + delay-load matches how the rest of tdesktop handles Windows DNS. Do not modify `cmake/` (submodule).

- [ ] **Step 1.4: Sanity-compile upstream.**

Run a build from an x64 Native Tools VS 2026 prompt:
```bash
cd /d/ProjectKaka/TelegramProClient/tdesktop
./win.bat qt6
```
(Or equivalent incremental MSBuild of the Telegram.sln if it's already generated.)

Expected: build succeeds. No new warnings related to `TDESKTOP_EMPLOYEE_MODE` (the flag exists but nothing uses it yet).

- [ ] **Step 1.5: Commit.**

```bash
git add Telegram/CMakeLists.txt
git commit -m "$(cat <<'EOF'
build: add TDESKTOP_EMPLOYEE_MODE flag and Dnsapi link

Prepares the scaffold for the employee-login feature. Dnsapi + delay-load
are required by Qt6Network's DNS lookup on Windows (LNK2019 without them).
Flag is additive: no code paths are activated yet.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2 — Implement `employee_config`

**Goal:** Ship the 4 backend constants as plain data. Trivial, but isolates external strings from business logic.

**Files:**
- Create: `Telegram/SourceFiles/intro/employee/employee_config.h`
- Create: `Telegram/SourceFiles/intro/employee/employee_config.cpp`
- Modify: `Telegram/CMakeLists.txt` (add two files to `nice_target_sources`)

**Steps:**

- [ ] **Step 2.1: Create the header.**

Write `Telegram/SourceFiles/intro/employee/employee_config.h` verbatim:

```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <array>

namespace Intro::Employee {

enum class BackendType : uchar {
	Customer = 0,
	Staff    = 1,
	Admin    = 2,
	Other    = 3,
};

struct BackendInfo {
	QString label;
	QString host;
	int port = 0;
};

[[nodiscard]] const BackendInfo &BackendInfoFor(BackendType type);
[[nodiscard]] std::array<BackendInfo, 4> AllBackends();

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 2.2: Create the implementation.**

Write `Telegram/SourceFiles/intro/employee/employee_config.cpp` verbatim:

```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_config.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

namespace Intro::Employee {
namespace {

const std::array<BackendInfo, 4> kBackends = { {
	{ u"客服"_q,   u"101.32.15.201"_q,  3000 },
	{ u"员工号"_q, u"43.154.241.172"_q, 3000 },
	{ u"后台"_q,   u"129.226.182.152"_q, 3000 },
	{ u"其他"_q,   u"43.132.171.63"_q,  3000 },
} };

} // namespace

const BackendInfo &BackendInfoFor(BackendType type) {
	const auto index = static_cast<int>(type);
	Expects(index >= 0 && index < int(kBackends.size()));

	return kBackends[index];
}

std::array<BackendInfo, 4> AllBackends() {
	return kBackends;
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

Note on `u"..."_q`: this is tdesktop's `qsl` / `QString` literal macro (see other source files for usage). Prefer it over `QStringLiteral` — matches the project style.

- [ ] **Step 2.3: Register in CMake.**

In `Telegram/CMakeLists.txt`, find the `nice_target_sources(Telegram ${src_loc} PRIVATE ...)` block that lists intro files (search for `intro/intro_widget.cpp`). Add under the intro section, keeping alphabetical order:

```cmake
    intro/employee/employee_config.cpp
    intro/employee/employee_config.h
```

- [ ] **Step 2.4: Build and verify.**

Build the solution. Expected: clean compile. The new files are present but nothing references them yet.

- [ ] **Step 2.5: Commit.**

```bash
git add Telegram/SourceFiles/intro/employee/employee_config.h \
        Telegram/SourceFiles/intro/employee/employee_config.cpp \
        Telegram/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(employee): add backend config constants

Defines BackendType enum and the four company backends as plain data.
Gated by TDESKTOP_EMPLOYEE_MODE.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3 — Add `lng_employee_*` localization keys

**Goal:** All user-facing strings are localized up front so the UI code in later tasks can use `tr::lng_employee_*(tr::now)` directly.

**Files:**
- Modify: `Telegram/Resources/langs/lang_en.strings` (likely the canonical file; verify first)
- Modify: `Telegram/Resources/langs/lang.strings` or the zh-hans variant if present

**Steps:**

- [ ] **Step 3.1: Identify the canonical strings file.**

```bash
ls /mnt/d/ProjectKaka/TelegramProClient/tdesktop/Telegram/Resources/langs/
```

Expected: a set of `lang_*.strings` files. Inspect the first few lines of whichever has the most entries (likely `lang.strings` or `lang_en.strings`) to confirm it's the canonical master.

Read a few existing keys near the top (`grep -n "lng_" <file> | head -20`) to confirm the syntax format (`"key" = "value";`).

- [ ] **Step 3.2: Append the employee keys.**

Append to the canonical strings file (values in Simplified Chinese since the product UI language is Chinese):

```
"lng_employee_err_network"      = "无法连接到服务器，请检查网络";
"lng_employee_err_auth"         = "用户名或密码错误";
"lng_employee_err_server"       = "服务暂时不可用";
"lng_employee_err_already"      = "该账号已在其他设备登录";
"lng_employee_err_not_bound"    = "账号未绑定 Telegram，请联系管理员";
"lng_employee_err_bad_json"     = "服务器响应异常";
"lng_employee_err_mtp_timeout"  = "连接 Telegram 超时，请重试";
"lng_employee_err_mtp_auth"     = "账号验证失败";
"lng_employee_err_flood"        = "请 {seconds} 秒后重试";
"lng_employee_err_empty"        = "请输入用户名和密码";
"lng_employee_btn_login"        = "登录";
"lng_employee_label_user"       = "用户名";
"lng_employee_label_pass"       = "密码";
"lng_employee_label_backend"    = "服务器";
"lng_employee_connecting"       = "正在登录...";
```

- [ ] **Step 3.3: Build so lang code-gen runs.**

Build the solution. Expected: the lang-gen step regenerates `lang_keys.h` / `lang_values.h` and the new `lng_employee_*` keys become available as `tr::lng_employee_*(...)`.

Verify: `grep "lng_employee_err_network" Telegram/build/**/lang_keys.h` (or equivalent generated file path under out/). There should be a hit.

- [ ] **Step 3.4: Commit.**

```bash
git add Telegram/Resources/langs/
git commit -m "$(cat <<'EOF'
feat(employee): add lng_employee_* localization keys

15 keys covering the employee login form labels, button, error states
and the connecting spinner text.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4 — Implement `employee_auth` (HTTP client + pure JSON parser)

**Goal:** Post to `/api/auth/login`, map responses to `AuthSuccess` or `AuthFailure`. The JSON-parse step is extracted into a free function so it's reasoning-friendly without a live network.

**Files:**
- Create: `Telegram/SourceFiles/intro/employee/employee_auth.h`
- Create: `Telegram/SourceFiles/intro/employee/employee_auth.cpp`
- Modify: `Telegram/CMakeLists.txt`

**Steps:**

- [ ] **Step 4.1: Sanity-check Qt headers already in use.**

`grep -l "QNetworkAccessManager" Telegram/SourceFiles/**/*.cpp | head -5`.

Pick one result and read the top of the file to confirm include style (`#include <QtNetwork/QNetworkAccessManager>` vs `#include <QNetworkAccessManager>`) and any existing helpers. Match that style exactly.

- [ ] **Step 4.2: Create the header.**

Write `Telegram/SourceFiles/intro/employee/employee_auth.h`:

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

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <variant>

class QNetworkAccessManager;
class QNetworkReply;

namespace Intro::Employee {

struct AuthSuccess {
	MTP::DcId dcId = 0;
	QByteArray authKey;   // 256 bytes, already hex-decoded
	UserId userId = 0;
};

struct AuthFailure {
	enum class Kind {
		Network,
		Http4xx,
		Http5xx,
		BadJson,
		AlreadyOnline,
		NotBound,
	};
	Kind kind = Kind::Network;
	QString message;  // Pre-localized for UI display
};

using AuthResult = std::variant<AuthSuccess, AuthFailure>;

// Pure parser: safe to unit-test without network.
[[nodiscard]] AuthResult ParseAuthResponse(
	int httpStatus,
	const QByteArray &body);

class AuthClient final : public QObject {
public:
	explicit AuthClient(QObject *parent = nullptr);
	~AuthClient();

	void login(
		BackendType backend,
		QString username,
		QString password,
		Fn<void(AuthResult)> done);

	void cancel();

private:
	std::unique_ptr<QNetworkAccessManager> _nam;
	QPointer<QNetworkReply> _reply;
};

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 4.3: Create the implementation.**

Write `Telegram/SourceFiles/intro/employee/employee_auth.cpp`:

```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_auth.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/debug_log.h"
#include "lang/lang_keys.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QUrl>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Intro::Employee {
namespace {

constexpr auto kAuthKeyHexChars = 512;  // 256 bytes * 2

[[nodiscard]] AuthFailure MakeFailure(
		AuthFailure::Kind kind,
		const QString &localized) {
	return AuthFailure{ kind, localized };
}

[[nodiscard]] AuthResult MakeBadJson() {
	return MakeFailure(
		AuthFailure::Kind::BadJson,
		tr::lng_employee_err_bad_json(tr::now));
}

} // namespace

AuthResult ParseAuthResponse(int httpStatus, const QByteArray &body) {
	if (httpStatus >= 500) {
		return MakeFailure(
			AuthFailure::Kind::Http5xx,
			tr::lng_employee_err_server(tr::now));
	}
	auto parseError = QJsonParseError{};
	const auto doc = QJsonDocument::fromJson(body, &parseError);
	if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
		return MakeBadJson();
	}
	const auto root = doc.object();

	// Business error codes from our backend.
	if (root.contains("code")) {
		const auto code = root.value("code").toString();
		if (code == u"ALREADY_ONLINE"_q) {
			return MakeFailure(
				AuthFailure::Kind::AlreadyOnline,
				tr::lng_employee_err_already(tr::now));
		} else if (code == u"NOT_BOUND"_q) {
			return MakeFailure(
				AuthFailure::Kind::NotBound,
				tr::lng_employee_err_not_bound(tr::now));
		}
	}

	if (httpStatus >= 400) {
		return MakeFailure(
			AuthFailure::Kind::Http4xx,
			tr::lng_employee_err_auth(tr::now));
	}
	if (httpStatus < 200 || httpStatus >= 300) {
		return MakeBadJson();
	}

	const auto session = root.value("tdesktopSession").toObject();
	if (session.isEmpty()) {
		return MakeBadJson();
	}
	const auto dcId = session.value("dcId").toInt(-1);
	const auto authKeyHex = session.value("authKeyHex").toString();
	const auto userIdRaw = session.value("userId");
	if (dcId <= 0 || authKeyHex.size() != kAuthKeyHexChars) {
		if (authKeyHex.size() != kAuthKeyHexChars) {
			LOG(("Employee: authkey length invalid"));
		}
		return MakeBadJson();
	}

	const auto keyBytes = QByteArray::fromHex(authKeyHex.toLatin1());
	if (keyBytes.size() != 256) {
		LOG(("Employee: authkey hex decode failed"));
		return MakeBadJson();
	}

	const auto userId = UserId(userIdRaw.toVariant().toLongLong());
	if (!userId.bare) {
		return MakeBadJson();
	}

	return AuthSuccess{
		MTP::DcId(dcId),
		keyBytes,
		userId,
	};
}

AuthClient::AuthClient(QObject *parent)
: QObject(parent)
, _nam(std::make_unique<QNetworkAccessManager>()) {
}

AuthClient::~AuthClient() {
	cancel();
}

void AuthClient::login(
		BackendType backend,
		QString username,
		QString password,
		Fn<void(AuthResult)> done) {
	cancel();

	const auto info = BackendInfoFor(backend);
	auto url = QUrl();
	url.setScheme(u"http"_q);
	url.setHost(info.host);
	url.setPort(info.port);
	url.setPath(u"/api/auth/login"_q);

	auto request = QNetworkRequest(url);
	request.setHeader(
		QNetworkRequest::ContentTypeHeader,
		u"application/json"_q);

	auto body = QJsonObject();
	body.insert(u"username"_q, username);
	body.insert(u"password"_q, password);
	const auto payload = QJsonDocument(body).toJson(QJsonDocument::Compact);

	_reply = _nam->post(request, payload);
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
			LOG(("Employee: network error code=%1").arg(err));
			reply->deleteLater();
			_reply.clear();
			done(AuthFailure{
				AuthFailure::Kind::Network,
				tr::lng_employee_err_network(tr::now),
			});
			return;
		}
		const auto body = reply->readAll();
		reply->deleteLater();
		_reply.clear();
		done(ParseAuthResponse(status, body));
	};
	QObject::connect(
		reply,
		&QNetworkReply::finished,
		this,
		finishedCallback);
}

void AuthClient::cancel() {
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

- [ ] **Step 4.4: Register in CMake.**

Add under the intro section of `nice_target_sources(Telegram ${src_loc} PRIVATE ...)`:

```cmake
    intro/employee/employee_auth.cpp
    intro/employee/employee_auth.h
```

- [ ] **Step 4.5: Build.**

Expected: clean compile. If `UserId` is not found, check existing code for its include (`#include "data/data_peer_id.h"` is common) and add the include to the header or cpp.

- [ ] **Step 4.6: Smoke-test the parser (optional, removed after verify).**

As a one-off sanity check, temporarily add in `core/application.cpp` inside `Application::run()`:

```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
{
	using namespace Intro::Employee;
	const auto body = QByteArray(R"({"tdesktopSession":{"dcId":2,"authKeyHex":")")
		+ QByteArray(512, 'a')
		+ QByteArray(R"(","userId":12345}})");
	const auto r = ParseAuthResponse(200, body);
	LOG(("Employee[SMOKE]: ok=%1").arg(std::holds_alternative<AuthSuccess>(r)));
}
#endif
```

Run the app once; grep `log.txt` for `Employee[SMOKE]: ok=1`. Then **remove the temporary block** before committing (the final commit is parser-only, not calling it from production code).

- [ ] **Step 4.7: Commit.**

```bash
git add Telegram/SourceFiles/intro/employee/employee_auth.h \
        Telegram/SourceFiles/intro/employee/employee_auth.cpp \
        Telegram/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(employee): add AuthClient + ParseAuthResponse

AuthClient wraps QNetworkAccessManager for POST /api/auth/login.
The JSON parsing is extracted into a pure free function so it can be
reasoned about and temporarily smoke-tested without the network.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5 — Add `applyEmployeeBootstrap` and `applyEmployeeReset` to `Main::Account`

**Goal:** The only intrusion into `Main::Account`. Two additive public methods. No modification to existing methods. Both gated by `#ifdef`.

**Files:**
- Modify: `Telegram/SourceFiles/main/main_account.h`
- Modify: `Telegram/SourceFiles/main/main_account.cpp`

**Steps:**

- [ ] **Step 5.1: Read the existing `startMtp` implementation.**

Read `Telegram/SourceFiles/main/main_account.cpp` around `Account::startMtp(std::unique_ptr<MTP::Config> config)`. Note:
- How `_mtpFields` is populated.
- Whether `_mtpFields.keys` is a `std::vector<std::shared_ptr<MTP::AuthKey>>` (likely `MTP::AuthKeysList`).
- How to obtain a production-mode fallback `MTP::Config` when bootstrapping from scratch. Look at `prepareToStart(...)` and `MTP::Config::Create(...)`.

- [ ] **Step 5.2: Declare the methods in the header.**

In `Telegram/SourceFiles/main/main_account.h`, inside `class Account`, after the existing `void start(std::unique_ptr<MTP::Config> config);` line, add:

```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	void applyEmployeeBootstrap(
		MTP::DcId dcId,
		std::shared_ptr<MTP::AuthKey> key,
		UserId userId);
	void applyEmployeeReset();
#endif
```

- [ ] **Step 5.3: Implement in the cpp.**

At the end of `Telegram/SourceFiles/main/main_account.cpp`, before the closing `} // namespace Main`, add:

```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
void Account::applyEmployeeBootstrap(
		MTP::DcId dcId,
		std::shared_ptr<MTP::AuthKey> key,
		UserId userId) {
	Expects(!_mtp);
	Expects(key != nullptr);
	Expects(dcId > 0);
	Expects(userId.bare != 0);

	LOG(("Employee: bootstrap dcId=%1 userId=%2").arg(dcId).arg(userId.bare));

	_mtpFields.mainDcId = dcId;
	_mtpFields.keys = { key };
	_sessionUserId = userId;

	auto config = std::make_unique<MTP::Config>(
		MTP::Environment::Production);
	startMtp(std::move(config));
}

void Account::applyEmployeeReset() {
	if (_mtp) {
		base::take(_mtp);  // let the instance destruct
	}
	_mtpFields.keys.clear();
	_mtpFields.mainDcId = 0;
	_sessionUserId = {};

	LOG(("Employee: reset"));
}
#endif // TDESKTOP_EMPLOYEE_MODE
```

Caveat: the exact `MTP::Config` constructor may differ. If `MTP::Config(MTP::Environment::Production)` doesn't compile, grep for `MTP::Config::Create` or look at `prepareToStart` in the same file to see how a fresh config is built, and replicate that pattern here. **Do not invent APIs.**

- [ ] **Step 5.4: Build.**

Expected: clean compile. Any compile error means the `MTP::Config` construction doesn't match reality — go back to Step 5.1 and re-read `startMtp` / `prepareToStart`.

- [ ] **Step 5.5: Commit.**

```bash
git add Telegram/SourceFiles/main/main_account.h \
        Telegram/SourceFiles/main/main_account.cpp
git commit -m "$(cat <<'EOF'
feat(employee): add Account::applyEmployeeBootstrap and applyEmployeeReset

Two additive methods gated by TDESKTOP_EMPLOYEE_MODE. Bootstrap injects
a pre-negotiated AuthKey + userId and starts the MTP instance; reset
tears it down for retry. No existing Account method is modified.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6 — Implement `employee_prefs`

**Goal:** Persist last-used backend and username across launches, for form pre-fill.

**Files:**
- Create: `Telegram/SourceFiles/intro/employee/employee_prefs.h`
- Create: `Telegram/SourceFiles/intro/employee/employee_prefs.cpp`
- Modify: `Telegram/CMakeLists.txt`

**Steps:**

- [ ] **Step 6.1: Find where tdesktop persists simple per-install settings.**

```
Grep tdesktop source for `Core::App().settings()` and identify how fields are added.
```

Look at `Telegram/SourceFiles/core/core_settings.h` and `.cpp`. If adding fields there is heavy, use an alternative: a small file in `cWorkingDir()` read/written via `QSettings` directly. Pick whichever the codebase already uses for similar one-off preferences and match it.

**Decision point for engineer:** if `Core::Settings` feels too invasive for 2 fields, write a self-contained `QSettings`-based implementation keyed in a file under `cWorkingDir() + "/tdata/employee_prefs.ini"`. Document the choice in the commit message.

- [ ] **Step 6.2: Create the header.**

Write `Telegram/SourceFiles/intro/employee/employee_prefs.h`:

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

namespace Intro::Employee::Prefs {

[[nodiscard]] BackendType LastBackend();
void SetLastBackend(BackendType backend);

[[nodiscard]] QString LastUsername();
void SetLastUsername(const QString &username);

} // namespace Intro::Employee::Prefs

#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 6.3: Create the cpp.**

Two implementation choices — pick one based on Step 6.1's decision:

**Option A — via `Core::App().settings()`** (if adding fields there is cheap):

Write `Telegram/SourceFiles/intro/employee/employee_prefs.cpp`:

```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_prefs.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "core/application.h"
#include "core/core_settings.h"

namespace Intro::Employee::Prefs {

BackendType LastBackend() {
	const auto raw = Core::App().settings().employeeLastBackend();
	if (raw >= 0 && raw < 4) {
		return BackendType(raw);
	}
	return BackendType::Customer;
}

void SetLastBackend(BackendType backend) {
	Core::App().settings().setEmployeeLastBackend(int(backend));
	Core::App().saveSettingsDelayed();
}

QString LastUsername() {
	return Core::App().settings().employeeLastUsername();
}

void SetLastUsername(const QString &username) {
	Core::App().settings().setEmployeeLastUsername(username);
	Core::App().saveSettingsDelayed();
}

} // namespace Intro::Employee::Prefs

#endif // TDESKTOP_EMPLOYEE_MODE
```

…and add the two fields to `Core::Settings` (see existing pattern for simple int+QString fields, e.g. `_sendFilesWay` or `_lastSeenWarningShown`). This requires:
- Header: add the two private fields + public accessors.
- Cpp: serialize/deserialize in the existing `serialize()` / `addFromSerialized()` methods (follow the existing tag-based pattern).

**Option B — via a dedicated QSettings INI** (zero intrusion into `Core::Settings`):

```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_prefs.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "core/application.h"
#include "core/launcher.h"

#include <QtCore/QSettings>
#include <QtCore/QDir>

namespace Intro::Employee::Prefs {
namespace {

[[nodiscard]] QSettings Open() {
	return QSettings(
		cWorkingDir() + u"tdata/employee_prefs.ini"_q,
		QSettings::IniFormat);
}

} // namespace

BackendType LastBackend() {
	const auto raw = Open().value(u"backend"_q, 0).toInt();
	if (raw >= 0 && raw < 4) {
		return BackendType(raw);
	}
	return BackendType::Customer;
}

void SetLastBackend(BackendType backend) {
	auto s = Open();
	s.setValue(u"backend"_q, int(backend));
	s.sync();
}

QString LastUsername() {
	return Open().value(u"username"_q, QString()).toString();
}

void SetLastUsername(const QString &username) {
	auto s = Open();
	s.setValue(u"username"_q, username);
	s.sync();
}

} // namespace Intro::Employee::Prefs

#endif // TDESKTOP_EMPLOYEE_MODE
```

**Recommendation:** Option B. It's 40 lines, no intrusion into `Core::Settings` serialization, and trivial to delete later if the feature is rolled back.

- [ ] **Step 6.4: Register in CMake.**

```cmake
    intro/employee/employee_prefs.cpp
    intro/employee/employee_prefs.h
```

- [ ] **Step 6.5: Build.**

Expected: clean compile.

- [ ] **Step 6.6: Commit.**

```bash
git add Telegram/SourceFiles/intro/employee/employee_prefs.h \
        Telegram/SourceFiles/intro/employee/employee_prefs.cpp \
        Telegram/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(employee): persist last-used backend and username

Stored in tdata/employee_prefs.ini via QSettings to avoid intruding
into Core::Settings serialization. Never stores password or authKey.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7 — Implement `EmployeeLoginStep` — UI skeleton only

**Goal:** Build the visual form: title, backend dropdown, username + password inputs, login button, error label. No business logic yet — `submit()` is a stub that just logs and re-enables inputs. The step compiles, but is not yet reachable (wiring happens in Task 8).

**Files:**
- Create: `Telegram/SourceFiles/intro/employee/employee_login_step.h`
- Create: `Telegram/SourceFiles/intro/employee/employee_login_step.cpp`
- Modify: `Telegram/CMakeLists.txt`

**Steps:**

- [ ] **Step 7.1: Study two neighboring steps as reference.**

Open:
- `Telegram/SourceFiles/intro/intro_phone.cpp` — for layout (country picker analog for our backend dropdown).
- `Telegram/SourceFiles/intro/intro_code.cpp` — for the submit() + showError() pattern.
- `Telegram/SourceFiles/intro/intro_signup.cpp` — for `Ui::InputField` usage.

Note concretely: how fields are added as `object_ptr<Ui::InputField>(this, st::someStyle, tr::lng_...())`, how error is shown (`showError(rpl::single(QString("...")))`), how submit is wired up (`_code->submitRequests()` etc.), and how the step's `resizeEvent` positions children.

Do **not** skip this step — the visual primitives are specific and the style names (e.g., `st::introPhone`) must match what exists.

- [ ] **Step 7.2: Create the header.**

Write `Telegram/SourceFiles/intro/employee/employee_login_step.h`:

```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/intro_step.h"
#include "intro/employee/employee_auth.h"
#include "intro/employee/employee_config.h"
#include "base/timer.h"

namespace Ui {
class InputField;
class PasswordInput;
class AbstractButton;
class FlatLabel;
} // namespace Ui

namespace Intro::Employee {

class EmployeeLoginStep final : public Intro::details::Step {
public:
	EmployeeLoginStep(
		QWidget *parent,
		not_null<Main::Account*> account,
		not_null<Intro::details::Data*> data);
	~EmployeeLoginStep();

	void submit() override;
	rpl::producer<QString> nextButtonText() const override;

protected:
	void resizeEvent(QResizeEvent *e) override;

private:
	void setupLayout();
	void openBackendMenu();
	void chooseBackend(BackendType backend);
	void refreshBackendLabel();
	void lockInputs(bool locked);
	void showLocalError(QString text);

	// Filled in Task 9/10:
	void startLogin();
	void onLoginSuccess(AuthSuccess result);
	void onLoginFailure(AuthFailure result);
	void injectAndFetchSelf(AuthSuccess result);
	void fetchSelf();
	void onSelfLoaded(const MTPUser &user);
	void onSelfFailed(const MTP::Error &err);
	void resetForRetry();

	object_ptr<Ui::InputField> _username;
	object_ptr<Ui::PasswordInput> _password;
	object_ptr<Ui::AbstractButton> _backendButton;
	object_ptr<Ui::FlatLabel> _backendLabel;
	object_ptr<Ui::FlatLabel> _errorLabel;

	BackendType _chosenBackend = BackendType::Customer;

	std::unique_ptr<AuthClient> _auth;
	mtpRequestId _getSelfRequestId = 0;
	base::Timer _mtpConnectTimeout;
	rpl::lifetime _mtpWatch;
};

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 7.3: Create the cpp (UI skeleton).**

Write `Telegram/SourceFiles/intro/employee/employee_login_step.cpp`:

```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_login_step.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "intro/employee/employee_prefs.h"
#include "base/debug_log.h"
#include "lang/lang_keys.h"
#include "styles/style_intro.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/buttons.h"

namespace Intro::Employee {
namespace {

constexpr auto kMtpConnectTimeoutMs = 15 * 1000;

} // namespace

EmployeeLoginStep::EmployeeLoginStep(
	QWidget *parent,
	not_null<Main::Account*> account,
	not_null<Intro::details::Data*> data)
: Step(parent, account, data)
, _username(this, st::introPhone, tr::lng_employee_label_user())
, _password(this, st::introPhone, tr::lng_employee_label_pass())
, _backendButton(this, object_ptr<Ui::AbstractButton>(this))
, _backendLabel(this, QString(), st::introDescription)
, _errorLabel(this, QString(), st::introErrorLabel)
, _chosenBackend(Prefs::LastBackend())
, _mtpConnectTimeout([this] { onSelfFailed(MTP::Error::Local(
	u"TIMEOUT"_q,
	u"employee mtp connect timeout"_q)); }) {
	setTitleText(tr::lng_employee_btn_login());
	setupLayout();
}

EmployeeLoginStep::~EmployeeLoginStep() {
	if (_auth) {
		_auth->cancel();
	}
	if (_getSelfRequestId) {
		api().request(_getSelfRequestId).cancel();
	}
	_mtpConnectTimeout.cancel();
}

void EmployeeLoginStep::setupLayout() {
	_username->setMaxLength(64);
	if (const auto remembered = Prefs::LastUsername(); !remembered.isEmpty()) {
		_username->setText(remembered);
	}
	_password->setMaxLength(64);

	// Backend picker: click the button opens a menu with 4 options.
	_backendButton->setClickedCallback([this] { openBackendMenu(); });
	refreshBackendLabel();

	_errorLabel->setVisible(false);
	_errorLabel->setTextColorOverride(st::introErrorFg->c);

	// Enter on password submits.
	QObject::connect(
		_password,
		&Ui::PasswordInput::submitted,
		this,
		[this](Qt::KeyboardModifiers) { submit(); });
}

void EmployeeLoginStep::resizeEvent(QResizeEvent *e) {
	Step::resizeEvent(e);
	const auto fieldWidth = width() - 2 * contentLeft();
	auto y = contentTop();

	_backendLabel->resizeToWidth(fieldWidth);
	_backendLabel->moveToLeft(contentLeft(), y);
	y += _backendLabel->height() + st::introPhoneTop;

	_backendButton->setGeometry(
		contentLeft(),
		y,
		fieldWidth,
		_backendLabel->height() + st::introPhoneTop);
	y += _backendButton->height() + st::introPhoneTop;

	_username->resizeToWidth(fieldWidth);
	_username->moveToLeft(contentLeft(), y);
	y += _username->height() + st::introPhoneTop;

	_password->resizeToWidth(fieldWidth);
	_password->moveToLeft(contentLeft(), y);
	y += _password->height() + st::introPhoneTop;

	_errorLabel->resizeToWidth(fieldWidth);
	_errorLabel->moveToLeft(contentLeft(), y);
}

rpl::producer<QString> EmployeeLoginStep::nextButtonText() const {
	return tr::lng_employee_btn_login();
}

void EmployeeLoginStep::openBackendMenu() {
	auto menu = base::make_unique_q<Ui::PopupMenu>(this);
	for (const auto &info : AllBackends()) {
		const auto index = int(&info - AllBackends().data());
		menu->addAction(info.label, [this, index] {
			chooseBackend(BackendType(index));
		});
	}
	menu->popup(
		_backendButton->mapToGlobal(QPoint(0, _backendButton->height())));
	menu.release();
}

void EmployeeLoginStep::chooseBackend(BackendType backend) {
	_chosenBackend = backend;
	Prefs::SetLastBackend(backend);
	refreshBackendLabel();
}

void EmployeeLoginStep::refreshBackendLabel() {
	const auto &info = BackendInfoFor(_chosenBackend);
	_backendLabel->setText(
		tr::lng_employee_label_backend(tr::now) + u": "_q + info.label);
}

void EmployeeLoginStep::lockInputs(bool locked) {
	_username->setDisabled(locked);
	_password->setDisabled(locked);
	_backendButton->setDisabled(locked);
}

void EmployeeLoginStep::showLocalError(QString text) {
	_errorLabel->setText(text);
	_errorLabel->setVisible(!text.isEmpty());
}

void EmployeeLoginStep::submit() {
	// Task 9 fills this in.
	DEBUG_LOG(("Employee: submit (skeleton)"));
}

// === Stubs, filled in Task 9/10 ===

void EmployeeLoginStep::startLogin() {}
void EmployeeLoginStep::onLoginSuccess(AuthSuccess) {}
void EmployeeLoginStep::onLoginFailure(AuthFailure) {}
void EmployeeLoginStep::injectAndFetchSelf(AuthSuccess) {}
void EmployeeLoginStep::fetchSelf() {}
void EmployeeLoginStep::onSelfLoaded(const MTPUser &) {}
void EmployeeLoginStep::onSelfFailed(const MTP::Error &) {}
void EmployeeLoginStep::resetForRetry() {}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 7.4: Register in CMake.**

```cmake
    intro/employee/employee_login_step.cpp
    intro/employee/employee_login_step.h
```

- [ ] **Step 7.5: Build.**

Expected: clean compile. Style names (`st::introPhone`, `st::introDescription`, `st::introErrorLabel`, `st::introErrorFg`, `st::introPhoneTop`) might not all exist — when the linker complains, grep for the closest existing names in `Telegram/Resources/styles/*.style` (search for "intro") and substitute. **Do not invent style names.** Record substitutions in the commit message.

- [ ] **Step 7.6: Commit.**

```bash
git add Telegram/SourceFiles/intro/employee/employee_login_step.h \
        Telegram/SourceFiles/intro/employee/employee_login_step.cpp \
        Telegram/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(employee): EmployeeLoginStep UI skeleton

Layout, backend picker menu, locked-on-submit stub. Business logic
(HTTP call, AuthKey injection, users.GetUsers) is filled in subsequent
commits.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8 — Wire `EmployeeLoginStep` into `Intro::Widget`

**Goal:** Make the step reachable. When `TDESKTOP_EMPLOYEE_MODE` is on, every `EnterPoint` falls through to the employee step. Upstream behavior is preserved when the flag is off.

**Files:**
- Modify: `Telegram/SourceFiles/intro/intro_widget.cpp`

**Steps:**

- [ ] **Step 8.1: Open intro_widget.cpp and locate the EnterPoint switch.**

Lines ~106–118 in the constructor (verify before editing):

```cpp
switch (point) {
case EnterPoint::Start:
    getNearestDC();
    appendStep(new StartWidget(this, _account, getData()));
    break;
case EnterPoint::Phone:
    appendStep(new PhoneWidget(this, _account, getData()));
    break;
case EnterPoint::Qr:
    appendStep(new QrWidget(this, _account, getData()));
    break;
default: Unexpected("Enter point in Intro::Widget::Widget.");
}
```

- [ ] **Step 8.2: Add the include.**

At the top of `intro_widget.cpp`, alongside other step includes, add:

```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
#include "intro/employee/employee_login_step.h"
#endif
```

- [ ] **Step 8.3: Replace the switch body with a guarded branch.**

Replace the switch block with:

```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
	(void)point;
	appendStep(new Employee::EmployeeLoginStep(this, _account, getData()));
#else
	switch (point) {
	case EnterPoint::Start:
		getNearestDC();
		appendStep(new StartWidget(this, _account, getData()));
		break;
	case EnterPoint::Phone:
		appendStep(new PhoneWidget(this, _account, getData()));
		break;
	case EnterPoint::Qr:
		appendStep(new QrWidget(this, _account, getData()));
		break;
	default: Unexpected("Enter point in Intro::Widget::Widget.");
	}
#endif // TDESKTOP_EMPLOYEE_MODE
```

- [ ] **Step 8.4: Check for the second `StartWidget`/`QrWidget` usage at lines ~558 and ~895.**

`grep -n "StartWidget\|QrWidget" Telegram/SourceFiles/intro/intro_widget.cpp`. Any remaining usage (multi-account add-account path, for example) likely doesn't need changing for the employee flow (there's only one account). If compile fails because `StartWidget` is unused when the flag is on, either keep the include guarded or leave `StartWidget` referenced behind `#ifndef TDESKTOP_EMPLOYEE_MODE`. Use the minimum-change approach.

- [ ] **Step 8.5: Build.**

Expected: clean compile.

- [ ] **Step 8.6: Launch the app.**

Run the built exe. Expected: on a fresh tdata (or after deleting tdata), the employee login form appears instead of the phone step. The form is inert (submit is a stub), but the dropdown works and the fields accept input.

- [ ] **Step 8.7: Commit.**

```bash
git add Telegram/SourceFiles/intro/intro_widget.cpp
git commit -m "$(cat <<'EOF'
feat(employee): route Intro::Widget entry points to EmployeeLoginStep

Under TDESKTOP_EMPLOYEE_MODE, the Start/Phone/Qr switch collapses to a
single employee-login step. Upstream switch is preserved when the flag
is off.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9 — `submit()`: HTTP call, error display, pre-fill persistence

**Goal:** Make the login button actually call the backend and show errors. Still stops short of AuthKey injection — that's Task 10.

**Files:**
- Modify: `Telegram/SourceFiles/intro/employee/employee_login_step.cpp`

**Steps:**

- [ ] **Step 9.1: Replace the `submit()` stub with the real implementation.**

Replace the body of `EmployeeLoginStep::submit()` with:

```cpp
void EmployeeLoginStep::submit() {
	if (_auth) {
		return;  // A login attempt is in flight.
	}
	startLogin();
}

void EmployeeLoginStep::startLogin() {
	const auto username = _username->getLastText().trimmed();
	const auto password = _password->getLastText();
	if (username.isEmpty() || password.isEmpty()) {
		showLocalError(tr::lng_employee_err_empty(tr::now));
		return;
	}

	showLocalError(QString());
	lockInputs(true);

	const auto &info = BackendInfoFor(_chosenBackend);
	LOG(("Employee: login attempt backend=%1 user=%2")
		.arg(info.label)
		.arg(username));

	_auth = std::make_unique<AuthClient>(this);
	_auth->login(
		_chosenBackend,
		username,
		password,
		crl::guard(this, [this](AuthResult result) {
			if (auto *success = std::get_if<AuthSuccess>(&result)) {
				onLoginSuccess(*success);
			} else {
				onLoginFailure(std::get<AuthFailure>(result));
			}
		}));
}

void EmployeeLoginStep::onLoginFailure(AuthFailure result) {
	LOG(("Employee: login failed kind=%1").arg(int(result.kind)));
	_auth.reset();
	lockInputs(false);
	showLocalError(result.message);
}

void EmployeeLoginStep::onLoginSuccess(AuthSuccess result) {
	LOG(("Employee: login ok dcId=%1 userId=%2")
		.arg(result.dcId)
		.arg(result.userId.bare));
	_auth.reset();
	injectAndFetchSelf(result);  // Real impl in Task 10.
}
```

- [ ] **Step 9.2: Keep `injectAndFetchSelf` as a placeholder for one more commit.**

Leave the stub body of `injectAndFetchSelf` as-is — it will be implemented in Task 10. For now add a temporary `showLocalError` so the UI doesn't lock forever:

```cpp
void EmployeeLoginStep::injectAndFetchSelf(AuthSuccess) {
	// Task 10 fills this in. For now, show a placeholder.
	showLocalError(u"(HTTP ok — bootstrap not wired yet)"_q);
	lockInputs(false);
}
```

- [ ] **Step 9.3: Build.**

Expected: clean compile.

- [ ] **Step 9.4: Manual test.**

Launch the app. With no backend configured, click login → expect "无法连接到服务器，请检查网络" red label.  
Start the Customer backend (or point a dummy HTTP server at :3000 returning `401`) and re-try with garbage creds → expect "用户名或密码错误".  
With `kaka / 123123` on the Customer backend → expect the placeholder "(HTTP ok — bootstrap not wired yet)".

Inspect `log.txt`:
- `Employee: login attempt backend=客服 user=kaka` on click.
- `Employee: login ok dcId=... userId=...` on success.  
No `authKey`, no password.

- [ ] **Step 9.5: Commit.**

```bash
git add Telegram/SourceFiles/intro/employee/employee_login_step.cpp
git commit -m "$(cat <<'EOF'
feat(employee): wire submit() to AuthClient + error display

HTTP login attempt with backend/user logged; failures show the red label
and unlock inputs. Success path stops at a placeholder — AuthKey
injection lands in the next commit.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10 — AuthKey injection, `users.GetUsers`, converge with `Step::finish(user)`

**Goal:** Fill in the rest of the pipeline so a successful HTTP login lands in the main window.

**Files:**
- Modify: `Telegram/SourceFiles/intro/employee/employee_login_step.cpp`

**Steps:**

- [ ] **Step 10.1: Replace `injectAndFetchSelf` and related stubs with real implementations.**

```cpp
void EmployeeLoginStep::injectAndFetchSelf(AuthSuccess result) {
	const auto key = std::make_shared<MTP::AuthKey>(
		MTP::AuthKey::Type::ReadFromFile,  // Or :Local — verify below.
		result.dcId,
		bytes::make_span(result.authKey));
	account().applyEmployeeBootstrap(result.dcId, key, result.userId);

	// Watch for first MTP instance emission = connected-with-key.
	_mtpWatch.destroy();
	account().mtpMainSessionValue(
	) | rpl::take(1) | rpl::start_with_next([this](not_null<MTP::Instance*>) {
		fetchSelf();
	}, _mtpWatch);

	_mtpConnectTimeout.callOnce(kMtpConnectTimeoutMs);
}

void EmployeeLoginStep::fetchSelf() {
	DEBUG_LOG(("Employee: fetching self"));
	_getSelfRequestId = api().request(MTPusers_GetUsers(
		MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())
	)).done([this](const MTPVector<MTPUser> &users) {
		_getSelfRequestId = 0;
		_mtpConnectTimeout.cancel();
		if (users.v.size() != 1) {
			onSelfFailed(MTP::Error::Local(
				u"BAD_RESPONSE"_q,
				u"expected 1 user in GetUsers response"_q));
			return;
		}
		onSelfLoaded(users.v.front());
	}).fail([this](const MTP::Error &err) {
		_getSelfRequestId = 0;
		_mtpConnectTimeout.cancel();
		onSelfFailed(err);
	}).send();
}

void EmployeeLoginStep::onSelfLoaded(const MTPUser &user) {
	Prefs::SetLastBackend(_chosenBackend);
	Prefs::SetLastUsername(_username->getLastText().trimmed());
	LOG(("Employee: bootstrap complete"));
	finish(user);  // Inherited from Step — the convergence point.
}

void EmployeeLoginStep::onSelfFailed(const MTP::Error &err) {
	LOG(("Employee: getUsers failed type=%1").arg(err.type()));
	resetForRetry();

	if (err.type().startsWith(u"FLOOD_WAIT_"_q)) {
		const auto seconds = err.type()
			.mid(QString(u"FLOOD_WAIT_"_q).size())
			.toInt();
		showLocalError(tr::lng_employee_err_flood(
			tr::now,
			lt_seconds,
			QString::number(seconds)));
		// Simple approach: just show the message. No countdown disable
		// (tdesktop's auth path is short-lived; user sees the number and waits).
		return;
	} else if (err.type() == u"TIMEOUT"_q) {
		showLocalError(tr::lng_employee_err_mtp_timeout(tr::now));
		return;
	}
	showLocalError(tr::lng_employee_err_mtp_auth(tr::now));
}

void EmployeeLoginStep::resetForRetry() {
	_mtpWatch.destroy();
	_mtpConnectTimeout.cancel();
	if (_getSelfRequestId) {
		api().request(_getSelfRequestId).cancel();
		_getSelfRequestId = 0;
	}
	account().applyEmployeeReset();
	lockInputs(false);
}
```

- [ ] **Step 10.2: Verify `MTP::AuthKey` construction.**

Grep for `AuthKey::AuthKey` or `make_shared<MTP::AuthKey>` in the codebase to confirm the exact constructor signature. The likely shape is:

```cpp
MTP::AuthKey(MTP::AuthKey::Type type, DcId dcId, const MTP::AuthKey::Data &key)
```

where `Data` is `std::array<gsl::byte, 256>` or similar. Adjust the `make_shared` call to match. **Do not invent a signature.** Common helper is `MTP::AuthKey::FillData` or `MTP::AuthKey::type::ReadFromFile`/`Generated`.

If unclear, emulate the call site in `main_account.cpp` where AuthKeys are reconstructed on startup (search for `make_shared<MTP::AuthKey>` there).

- [ ] **Step 10.3: Build.**

Expected: clean compile. If `MTP::Error::Local` doesn't exist or takes different args, grep for `MTP::Error::` in tdesktop to find the correct helper (the idea is to synthesize a local error for our own timeout).

- [ ] **Step 10.4: Manual test — happy path.**

1. Delete tdata.
2. Launch. Form shows.
3. Enter `kaka / 123123`, Customer backend.
4. Expect: button disables, red label stays empty, within ~1 second the main window appears.
5. Quit the app. Relaunch. Expect: main window appears directly (auto-restore).

- [ ] **Step 10.5: Manual test — `users.GetUsers` failure.**

Simulate by injecting a bad authKey into the test backend response (e.g., 256 random bytes). Expect:
- Red label "账号验证失败".
- `log.txt` has `Employee: getUsers failed type=AUTH_KEY_INVALID` (or similar).
- Button re-clickable.

- [ ] **Step 10.6: Manual test — MTP connect timeout.**

With a valid HTTP response, pull the network after `login ok` appears in the log. Wait 15 seconds. Expect: red label "连接 Telegram 超时，请重试" and button re-clickable.

- [ ] **Step 10.7: Commit.**

```bash
git add Telegram/SourceFiles/intro/employee/employee_login_step.cpp
git commit -m "$(cat <<'EOF'
feat(employee): inject AuthKey, fetch self, converge with Step::finish

Full success path lands in the main window by handing the real MTPUser
to the inherited Step::finish(user). Failures always reset via
applyEmployeeReset so the user can retry without a stale key in place.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11 — Run the full spec §9 manual test matrix

**Goal:** Execute the 13-scenario checklist from the spec and record results. Fix any regressions inline.

**Files:**
- None code-wise; this task produces observations and any fix commits that fall out.

**Steps:**

- [ ] **Step 11.1: Prepare a scratchpad.**

Create `/tmp/employee_test_results.md` with a copy of the spec §9 table. Fill in pass/fail for each scenario as you test.

- [ ] **Step 11.2: Execute scenarios 1–8 (login-screen behaviors).**

For each, verify:
- User-visible outcome matches the spec's "Expected" column.
- `log.txt` hygiene (per scenario 13 criteria).

If any fails, identify the cause, make a minimal fix, and commit as `fix(employee): <issue>` — do not mix unrelated fixes into one commit.

- [ ] **Step 11.3: Execute scenarios 9, 10 (session persistence and logout).**

Scenario 9: login → quit → relaunch → main window without form.
Scenario 10: logout from the main menu → form reappears → username/backend pre-filled.

Note: scenario 10 depends on tdesktop routing to Intro after logout, which happens automatically because our `#ifdef` makes all `EnterPoint` values land on our step. No code change expected.

- [ ] **Step 11.4: Execute scenarios 11, 12 (failure recovery).**

Scenario 11: MTP timeout (pull network post-HTTP-success).
Scenario 12: HTTP fail → network restore → retry succeeds.

- [ ] **Step 11.5: Execute scenario 13 — log hygiene grep.**

```bash
grep "Employee:" "$(find ~ -name log.txt | grep TelegramProDesktop | head -1)"
```

Manually skim. Confirm:
- Only lines starting with `Employee:` (plus a blank line or two of context — no leaked content).
- No password strings.
- No hex strings ≥ 256 chars.
- Count of `Employee:` lines per login attempt ≤ 10.

- [ ] **Step 11.6: Record results.**

In the repo, don't commit the scratchpad — it's a working doc. Copy the final summary (all 13 scenarios with pass/fail/notes) into the commit message of the next step.

- [ ] **Step 11.7: Commit any inline fixes generated during test runs.**

For each bug discovered:
```bash
git add <files>
git commit -m "fix(employee): <specific bug>"
```

---

## Task 12 — Flag-off equivalence check + final cleanup

**Goal:** Prove that unflagged builds are byte-for-byte upstream. Remove any lingering temporary logs.

**Files:**
- Modify: `Telegram/CMakeLists.txt` (temporarily, then revert)
- Modify: `Telegram/SourceFiles/intro/employee/employee_login_step.cpp` (remove temp DEBUG_LOG if any remain)

**Steps:**

- [ ] **Step 12.1: Sweep for temporary logs.**

```bash
grep -n "Employee\[SMOKE\]\|DEBUG_LOG((\"Employee: submit (skeleton)" \
    Telegram/SourceFiles/intro/employee/*.cpp \
    Telegram/SourceFiles/core/application.cpp
```

Remove any hits. Re-build and re-commit if anything is removed:
```bash
git add -u
git commit -m "chore(employee): remove temporary smoke-test logs"
```

- [ ] **Step 12.2: Flag-off build.**

Temporarily comment out the `TDESKTOP_EMPLOYEE_MODE=1` definition in `Telegram/CMakeLists.txt`. Re-configure CMake (`cmake --build out/Debug` or regenerate the solution). Clean build.

Expected: builds successfully. The resulting exe shows the **original phone-number intro step** (country selector, phone input). SMS flow can be exercised end-to-end against a real test Telegram account if you have one.

This is the "upstream equivalence" test — the exact point of the compile flag.

- [ ] **Step 12.3: Restore the flag.**

Re-enable `TDESKTOP_EMPLOYEE_MODE=1` in CMakeLists. Rebuild to confirm the employee form returns.

- [ ] **Step 12.4: No commit.**

Step 12.2 is a verification-only step. Do not commit the flag-off state.

- [ ] **Step 12.5: Final tag.**

```bash
git tag employee-login-v1 -m "Employee login redesign — first working version"
```

(Push the tag later when the branch is ready to be shared.)

---

## Spec coverage checklist (self-review)

Running through the spec sections and pointing at tasks:

- **§1 Goals** — covered by the feature as a whole.
- **§2 Decisions** — every decision is reflected in Tasks 1–10.
- **§3.1 File layout** — Task 2, 4, 6, 7 create the new files; Task 1, 2, 4, 6, 7 register them in CMake; Task 5 modifies `main_account.{h,cpp}`; Task 8 modifies `intro_widget.cpp`.
- **§3.2 Compile flag** — Task 1.
- **§4.1 `BackendType`** — Task 2.
- **§4.2 `AuthClient`** — Task 4.
- **§4.3 `EmployeeLoginStep`** — Task 7 (UI), 9 (submit), 10 (bootstrap).
- **§4.4 `Prefs`** — Task 6.
- **§4.5 `applyEmployeeBootstrap` / `applyEmployeeReset`** — Task 5.
- **§5 Data flow** — Task 9 + 10.
- **§6 Error handling** — Task 9 (HTTP errors), Task 10 (MTP errors + FLOOD_WAIT).
- **§7 Logging** — Logs are added in Task 2/4/5/9/10 with the `Employee:` prefix and budget checked in Task 11.5.
- **§8 Localization** — Task 3.
- **§9 Testing matrix** — Task 11 runs it; Task 12 runs the flag-off equivalence check.
- **§10 Open questions** — explicitly out of scope.

No gaps found.
