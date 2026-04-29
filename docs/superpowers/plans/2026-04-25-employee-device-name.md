# Set Device Name From Server-Supplied employee.name on Login Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On successful employee login, parse `employee.name` from the server response and unconditionally write it into `Core::App().settings().customDeviceModel()`, so the next `MTPInitConnection` carries `device_model = <employee.name>` and the Telegram "Active Sessions" list displays the real employee name.

**Architecture:** Thread a new `QString employeeName` field through `AuthSuccess` → `applyEmployeeBootstrap`. At the top of `applyEmployeeBootstrap`, if name is non-empty, call `Core::App().settings().setCustomDeviceModel(name)` + `Core::App().saveSettingsDelayed()` BEFORE `startMtp(...)` — so the new `MTP::Instance` reads the updated value at construction (`mtp_instance.cpp:336`) and uses it for the very first `MTPInitConnection`. Empty / missing / non-string `name` skips the write defensively.

**Tech Stack:** C++; Qt JSON (`QJsonObject::value().toString()`); existing employee-login pipeline gated by `#ifdef TDESKTOP_EMPLOYEE_MODE`.

**Spec:** `docs/superpowers/specs/2026-04-25-employee-device-name-design.md` (dcad9d2)

**Branch baseline:** `customization` @ dcad9d2.

---

## Global Conventions

### G1. Already gated by `TDESKTOP_EMPLOYEE_MODE`
All edits land inside files that are already either inside `#ifdef TDESKTOP_EMPLOYEE_MODE` (the `employee_*` files entire-file-gated; `main_account.h:58` / `main_account.cpp:688` block-gated). No new gate needed.

### G2. No new files, no new includes
`Core::App().settings()` already used elsewhere in `main_account.cpp` (e.g. line 514). `core/application.h` already included at line 11. `QJsonObject::value().toString()` available — `<QJsonObject>` already included in `employee_auth.cpp:16`.

### G3. Chinese bullet summary before compile handoff
The implementer emits a Chinese bullet summary of what changed, then stops and hands to the user for compile + smoke. The user compiles on MSVC.

### G4. Workflow
The implementer writes code and self-reviews but does NOT compile or commit. The user compiles + smoke-tests; after confirmation, the controller commits and tags.

### G5. Mtime touch on every edited file
After every Edit/Write, run `touch <path>` on the file. WSL on `/mnt/d` (NTFS via 9P) sometimes skips mtime updates and MSBuild then thinks nothing changed. The user has been bitten by this twice in this session. Touching is mandatory.

### G6. Commit message style
```
feat(employee): set device name from server-supplied employee.name on login

<body>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Task 1: Thread employeeName through the login plumbing

This is **one logical change spread across five files**. The five edits are all applied before any compile, then the user runs a single Debug build. Steps below mirror the spec's §8 ordering.

### Files
Modify (5 hunks across 5 files):
- `Telegram/SourceFiles/intro/employee/employee_auth.h` — extend `AuthSuccess`
- `Telegram/SourceFiles/intro/employee/employee_auth.cpp` — extract `employee.name` in parser
- `Telegram/SourceFiles/main/main_account.h` — extend `applyEmployeeBootstrap` declaration
- `Telegram/SourceFiles/main/main_account.cpp` — match definition + insert `setCustomDeviceModel` block
- `Telegram/SourceFiles/intro/employee/employee_login_step.cpp` — pass `result.employeeName`

### Step 1: Context confirmation

- [ ] Verify all 5 anchor lines on baseline `dcad9d2`:

```bash
echo "--- AuthSuccess struct ---"
sed -n '24,30p' Telegram/SourceFiles/intro/employee/employee_auth.h
echo "--- ParseAuthResponse return ---"
sed -n '184,191p' Telegram/SourceFiles/intro/employee/employee_auth.cpp
echo "--- applyEmployeeBootstrap declaration ---"
sed -n '58,66p' Telegram/SourceFiles/main/main_account.h
echo "--- applyEmployeeBootstrap definition top ---"
sed -n '693,720p' Telegram/SourceFiles/main/main_account.cpp
echo "--- login_step caller ---"
sed -n '237,243p' Telegram/SourceFiles/intro/employee/employee_login_step.cpp
```

Expected: contents match what is referenced in Steps 2-6 below. Stop and report if any region drifted.

### Step 2: Add `employeeName` field to `AuthSuccess`

- [ ] Open `Telegram/SourceFiles/intro/employee/employee_auth.h`. Current struct (lines 24-30):

```cpp
struct AuthSuccess {
	MTP::DcId dcId = 0;
	QByteArray authKey;   // 256 bytes, already hex-decoded
	UserId userId = 0;
	QString token;                // Bearer token for /api/auth/verify
	PermissionValues permissions{}; // backend-declared permission bits
};
```

- [ ] Insert one line — `QString employeeName;` between `userId` and `token`. Final form:

```cpp
struct AuthSuccess {
	MTP::DcId dcId = 0;
	QByteArray authKey;   // 256 bytes, already hex-decoded
	UserId userId = 0;
	QString employeeName;         // From response root.employee.name; empty if absent
	QString token;                // Bearer token for /api/auth/verify
	PermissionValues permissions{}; // backend-declared permission bits
};
```

Use tab indent matching neighbors. The comment on the new field is a one-liner — do not expand it across multiple lines.

### Step 3: Extract `employee.name` in `ParseAuthResponse` and include in returned brace-init

- [ ] Open `Telegram/SourceFiles/intro/employee/employee_auth.cpp`. Current return block (lines 184-190):

```cpp
	return AuthSuccess{
		MTP::DcId(dcId),
		keyBytes,
		userId,
		token,
		permissions,
	};
```

- [ ] Immediately before this return, add a `const auto employeeName = ...;` line. Then add `employeeName,` to the brace-init between `userId,` and `token,`. Final form:

```cpp
	const auto employeeName = root.value(u"employee"_q)
		.toObject()
		.value(u"name"_q)
		.toString();

	return AuthSuccess{
		MTP::DcId(dcId),
		keyBytes,
		userId,
		employeeName,
		token,
		permissions,
	};
```

**Why this works defensively:**
- If `root` has no `employee` key → `value("employee")` returns `Undefined` → `toObject()` returns `{}` → `value("name").toString()` returns `""`.
- If `employee.name` is null / number / array → `toString()` returns `""`.
- The branch in `applyEmployeeBootstrap` will skip empty strings.

### Step 4: Extend `applyEmployeeBootstrap` declaration

- [ ] Open `Telegram/SourceFiles/main/main_account.h`. Current declaration (lines 59-65):

```cpp
	void applyEmployeeBootstrap(
		MTP::DcId dcId,
		std::shared_ptr<MTP::AuthKey> key,
		UserId userId,
		QString token,
		Intro::Employee::PermissionValues permissions,
		Intro::Employee::BackendType backend);
```

- [ ] Insert `QString employeeName,` between `userId,` and `QString token,`. Final form:

```cpp
	void applyEmployeeBootstrap(
		MTP::DcId dcId,
		std::shared_ptr<MTP::AuthKey> key,
		UserId userId,
		QString employeeName,
		QString token,
		Intro::Employee::PermissionValues permissions,
		Intro::Employee::BackendType backend);
```

### Step 5: Match definition + insert `setCustomDeviceModel` block

- [ ] Open `Telegram/SourceFiles/main/main_account.cpp`. Current top of definition (lines 693-713):

```cpp
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

	_employeeVerify->cancel();

	LOG(("Employee: bootstrap dcId=%1 userId=%2 tokenLen=%3 backend=%4"
		).arg(dcId
		).arg(userId.bare
		).arg(token.size()
		).arg(static_cast<uchar>(backend)));

	_employeeBackend = backend;
	_employeePermissions->apply(permissions, token);
```

- [ ] **Edit A** — match the new signature: insert `QString employeeName,` between `UserId userId,` and `QString token,`.
- [ ] **Edit B** — between the `LOG((...))` call (which closes around line 710) and the line `_employeeBackend = backend;`, insert the device-name block. Final form for the modified region:

```cpp
void Account::applyEmployeeBootstrap(
		MTP::DcId dcId,
		std::shared_ptr<MTP::AuthKey> key,
		UserId userId,
		QString employeeName,
		QString token,
		Intro::Employee::PermissionValues permissions,
		Intro::Employee::BackendType backend) {
	Expects(key != nullptr);
	Expects(dcId > 0);
	Expects(userId.bare != 0);

	_employeeVerify->cancel();

	LOG(("Employee: bootstrap dcId=%1 userId=%2 tokenLen=%3 backend=%4"
		).arg(dcId
		).arg(userId.bare
		).arg(token.size()
		).arg(static_cast<uchar>(backend)));

	if (!employeeName.isEmpty()) {
		Core::App().settings().setCustomDeviceModel(employeeName);
		Core::App().saveSettingsDelayed();
		LOG(("Employee: bootstrap set customDeviceModel len=%1"
			).arg(employeeName.size()));
	}

	_employeeBackend = backend;
	_employeePermissions->apply(permissions, token);
```

**Why this position:**
- Above `_employeeBackend = ...` and ALL of `_employeePermissions->apply`, `local().writeEmployeeAuth`, `_mtpFields = ...`, and `startMtp(...)`.
- `MTP::Instance::Private::start()` (`mtp_instance.cpp:336`) reads `Core::App().settings().customDeviceModel()` once at construction; since our `setCustomDeviceModel` runs before `startMtp`, the new instance picks up the employee name immediately and uses it in the first `MTPInitConnection`.
- The `LOG` line logs only the length (not the content) — defensive against putting employee names into log files.

**Why `saveSettingsDelayed`:**
- `setCustomDeviceModel` only mutates the in-memory `_customDeviceModel` Variable. `saveSettingsDelayed` schedules a flush to `tdata/settings*` so cold-restart preserves the value. The flush is asynchronous; MTP::Instance reads the in-memory value, NOT the on-disk file, so this scheduling does not affect the immediate behavior.

### Step 6: Pass `result.employeeName` from the caller

- [ ] Open `Telegram/SourceFiles/intro/employee/employee_login_step.cpp`. Current call (lines 237-243):

```cpp
	account().applyEmployeeBootstrap(
		result.dcId,
		std::move(key),
		result.userId,
		result.token,
		result.permissions,
		_chosenBackend);
```

- [ ] Insert `result.employeeName,` between `result.userId,` and `result.token,`. Final form:

```cpp
	account().applyEmployeeBootstrap(
		result.dcId,
		std::move(key),
		result.userId,
		result.employeeName,
		result.token,
		result.permissions,
		_chosenBackend);
```

### Step 7: Touch all 5 files (WSL/9P mtime defense)

- [ ] Run:

```bash
touch \
    Telegram/SourceFiles/intro/employee/employee_auth.h \
    Telegram/SourceFiles/intro/employee/employee_auth.cpp \
    Telegram/SourceFiles/main/main_account.h \
    Telegram/SourceFiles/main/main_account.cpp \
    Telegram/SourceFiles/intro/employee/employee_login_step.cpp
stat -c '%y  %n' \
    Telegram/SourceFiles/intro/employee/employee_auth.h \
    Telegram/SourceFiles/intro/employee/employee_auth.cpp \
    Telegram/SourceFiles/main/main_account.h \
    Telegram/SourceFiles/main/main_account.cpp \
    Telegram/SourceFiles/intro/employee/employee_login_step.cpp
```

Expected: all 5 mtimes are "now". This guards against the WSL/9P mtime-skip bug.

### Step 8: Self-review

- [ ] Run `git diff --stat`; expected exactly 5 files changed:

```
Telegram/SourceFiles/intro/employee/employee_auth.cpp        |  6 ++++++
Telegram/SourceFiles/intro/employee/employee_auth.h          |  1 +
Telegram/SourceFiles/intro/employee/employee_login_step.cpp  |  1 +
Telegram/SourceFiles/main/main_account.cpp                   |  9 +++++++++
Telegram/SourceFiles/main/main_account.h                     |  1 +
```

(Line counts are approximate; the file list must be exactly these five.)

- [ ] Run `git diff Telegram/SourceFiles/intro/employee/employee_auth.h` and confirm the only change is one inserted line `\tQString employeeName;` between `UserId userId = 0;` and `QString token;`.

- [ ] Run `git diff Telegram/SourceFiles/intro/employee/employee_auth.cpp` and confirm two inserted regions: (a) the `const auto employeeName = ...` block immediately before `return AuthSuccess{`; (b) the new `employeeName,` line in the brace-init between `userId,` and `token,`.

- [ ] Run `git diff Telegram/SourceFiles/main/main_account.h` and confirm the only change is one inserted parameter line `\t\tQString employeeName,` between `UserId userId,` and `QString token,`.

- [ ] Run `git diff Telegram/SourceFiles/main/main_account.cpp` and confirm exactly two inserted regions: (a) `QString employeeName,` parameter in the function signature; (b) the `if (!employeeName.isEmpty()) { ... }` block right before `_employeeBackend = backend;`.

- [ ] Run `git diff Telegram/SourceFiles/intro/employee/employee_login_step.cpp` and confirm the only change is one inserted line `\t\tresult.employeeName,` between `result.userId,` and `result.token,`.

- [ ] Run `grep -c "employeeName" Telegram/SourceFiles/intro/employee/employee_auth.h Telegram/SourceFiles/intro/employee/employee_auth.cpp Telegram/SourceFiles/main/main_account.h Telegram/SourceFiles/main/main_account.cpp Telegram/SourceFiles/intro/employee/employee_login_step.cpp` and confirm the per-file counts are roughly:
   - `employee_auth.h`: 1
   - `employee_auth.cpp`: 2
   - `main_account.h`: 1
   - `main_account.cpp`: 4 (1 parameter + 1 condition + 1 setter arg + 1 LOG)
   - `employee_login_step.cpp`: 1

- [ ] Run `grep -n "TDESKTOP_EMPLOYEE_MODE" Telegram/SourceFiles/main/main_account.cpp Telegram/SourceFiles/main/main_account.h | head -10` and confirm our edits in `main_account.{h,cpp}` are inside the existing `#ifdef TDESKTOP_EMPLOYEE_MODE` block (declaration line 58, definition block starting line 688). No new `#ifdef` introduced.

### Step 9: Chinese bullet summary + hand off

Emit:

```
编译前总结（employee device name 自动设置）：
- employee_auth.h AuthSuccess 加 QString employeeName 字段（位于 userId 与 token 之间）
- employee_auth.cpp ParseAuthResponse 中读取 root.employee.name，塞进返回的 AuthSuccess（缺失/null/非字符串 → 空串）
- main_account.h applyEmployeeBootstrap 声明加 QString employeeName 参数（位于 userId 与 token 之间）
- main_account.cpp 同步函数定义；在 _employeeBackend = backend; 之前插入：
    if (!employeeName.isEmpty()) {
        Core::App().settings().setCustomDeviceModel(employeeName);
        Core::App().saveSettingsDelayed();
        LOG(...len=...);
    }
  确保在 startMtp 之前写入 customDeviceModel，让首次 MTPInitConnection 直接用真实姓名
- employee_login_step.cpp 调用处补传 result.employeeName
- 5 个文件全部 touch 过，mtime 已刷
- 无新文件、无新 include、无新 #ifdef gate

请编译并 smoke：
  cmake --build out --config Debug --target Telegram

测试项：
  1. 用一个 employee.name = "测试员工A" 的账号登录 → Settings → Active Sessions 顶部那条显示 "测试员工A"（不是 BIOS / "Desktop"）
  2. 在另一台已登录该 Telegram 账号的客户端（手机 / Web）打开 Active Sessions → 也显示 "测试员工A"
  3. 退出再用同一员工登录 → 名字仍是 "测试员工A"（cold-start / 持久化回归）
  4. 后台改员工姓名为 "测试员工B" → 退出再登录 → 显示 "测试员工B"
  5. 在客户端 Settings → Active Sessions → 改设备名为 "我的电脑" → 保存 → 退出再登录 → 又被覆盖回 "测试员工B"（覆盖语义生效）
  6. 模拟服务端响应里没有 employee.name 字段 → 登录 → 设备名保持原值不变（防御性 skip）
  7. 验证 hide-user-username-v1 / hide-user-phone-v1 仍生效（相邻特性回归）
```

Wait for "编译通过" + smoke confirmation.

### Step 10: Commit

After user confirms compile + smoke pass:

```bash
git add \
    Telegram/SourceFiles/intro/employee/employee_auth.h \
    Telegram/SourceFiles/intro/employee/employee_auth.cpp \
    Telegram/SourceFiles/main/main_account.h \
    Telegram/SourceFiles/main/main_account.cpp \
    Telegram/SourceFiles/intro/employee/employee_login_step.cpp
git commit -m "$(cat <<'EOF'
feat(employee): set device name from server-supplied employee.name on login

Add QString employeeName to AuthSuccess; ParseAuthResponse now reads
root.employee.name into it (empty for missing / null / non-string).
Thread the value through applyEmployeeBootstrap; if non-empty, write
to Core::App().settings().customDeviceModel() and saveSettingsDelayed
before startMtp so the first MTPInitConnection carries the employee
name. Empty input is skipped defensively, preserving any prior value.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Step 11: Tag (do NOT push without confirmation)

```bash
git tag -a employee-device-name-v1 -m "$(cat <<'EOF'
Set device name from server-supplied employee.name on login

Active Sessions list shows the real employee name instead of BIOS
product name / "Desktop". Unconditional overwrite on every login;
empty / missing / non-string name preserves existing value. Logout
does not clear; cold-start reuses persisted customDeviceModel.
EOF
)"
```

Push only on explicit user request.

---

## Spec Coverage Self-Check

| Spec section | Covered by |
|---|---|
| §1 Success criterion 1 (next initConnection carries `employee.name`) | Step 5 (write before startMtp) |
| §1 Success criterion 2 (server authorization shows the name) | Side effect of #1 once Telegram processes initConnection |
| §1 Success criterion 3 (Active Sessions UI shows the name) | Smoke test §9.1, §9.2 |
| §1 Success criterion 4 (re-login after admin name-change) | Smoke test §9.4; "unconditional overwrite" enforced by Step 5 |
| §1 Success criterion 5 (empty name does NOT clobber) | Step 5 `if (!employeeName.isEmpty())`; smoke test §9.6 |
| §1 Success criterion 6 (cold-start preserves) | `customDeviceModel` is core_settings-persistent; no special handling needed; smoke test §9.3 |
| §1 Success criterion 7 (gated by `TDESKTOP_EMPLOYEE_MODE`) | Step 8 self-review check; all edited regions are inside existing gates |
| §2 Approach (5 surgical edits) | Steps 2-6 each describe one of the 5 edits |
| §2 Component (a) AuthSuccess field | Step 2 |
| §2 Component (b) ParseAuthResponse | Step 3 |
| §2 Component (c) login_step caller | Step 6 |
| §2 Component (d) main_account.h declaration | Step 4 |
| §2 Component (e) main_account.cpp definition + setCustomDeviceModel block | Step 5 |
| §2 Ordering vs startMtp | Step 5 explicit insertion point + commentary |
| §3 Data Flow | Step 5 commentary |
| §4 What Is Not Changed (Platform::DeviceModelPretty, Settings UI, /api/auth/verify, applyEmployeeReset, AuthSnapshot) | None of these files appear in Step 8 `git diff --stat`; verified by self-review |
| §5 Edge cases (missing/null/wrong type/empty/long/non-Latin/two employees/cold-start/manual edit/logout) | All flow through the `if (!employeeName.isEmpty())` guard or are handled by core_settings persistence; smoke tests §9.1, §9.3, §9.4, §9.5, §9.6 cover the observable subset |
| §6 Smoke tests 1-8 | Step 9 emits Chinese summary listing 7 of 8 (§9 omits the "non-employee build still compiles" item — that's tested by the existing CI / by user manually building without the flag) |
| §7 Risks (ordering, signature mismatch, save deferral, brace-init order, long names, includes, race) | Step 5 commentary + Step 8 self-review (per-file diff inspection) |
| §8 Implementation Ordering steps 1-10 | Maps to plan steps 1-10 |
