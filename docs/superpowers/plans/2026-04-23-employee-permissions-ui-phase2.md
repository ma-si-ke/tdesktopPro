# Employee Permissions Phase 2 — UI Disable + Periodic Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Disable every cataloged UI entry point (~60 sites) when the corresponding employee permission is `false`, and refresh permissions from the backend every 60 seconds with automatic hard logout on 401.

**Architecture:** A 3-function helper (`GuardAction` / `BindDisabled` / `Allowed`) inserted one-line-per-site across ~30 UI files. Refresh is a `base::Timer` in `Main::Account` that reuses the Phase 1 `VerifyClient`. Phase 1 semantics are preserved — this phase only adds additively.

**Tech Stack:** C++20, Qt 6.11, tdesktop lib_base / lib_ui / lib_rpl idioms.

**Spec:** `docs/superpowers/specs/2026-04-23-employee-permissions-ui-phase2.md` (d44818b)

**Phase 1 baseline:** tag `employee-permissions-v1` @ 63ccfe7.

---

## Global Conventions

These rules apply to every task. The implementer subagent MUST follow them.

### G1. All new code gated

Every new declaration, include, call, and member MUST be wrapped in:

```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
...
#endif
```

This includes: `#include "intro/employee/employee_ui_guard.h"` at the top of UI files; the guard call itself; new `Main::Account` members and methods.

### G2. Context-first: grep before editing

Before touching any file, grep for:
- the stable anchor string given in the task (a `tr::lng_*` key or function name)
- surrounding patterns the file already uses for enable/disable (e.g., `peer->canSendAnything()`, `->setEnabled(`, `->setDisabled(`, `rpl::combine`, `rpl::start_with_next`)

Follow the file's existing style (spaces vs. tabs, namespace qualification, capture-by-copy vs. by-ref) rather than intuition.

### G3. Line numbers in tables may have drifted

The spec lists `file:line` references from a prior audit. The anchor (a `tr::lng_*` string or function name) is the stable locator; the line number is a starting hint. Always grep for the anchor first.

### G4. Chinese bullet summary before every compile

Immediately before the user runs a build, emit a Chinese bullet summary of what changed since the last build, e.g.:

```
编译前总结：
- 新增 intro/employee/employee_ui_guard.{h,cpp}
- CMakeLists.txt 注册两个新文件
- 未改动任何 UI 文件
```

### G5. Compile commands

- User builds on Windows MSVC via their IDE; the subagent MUST NOT attempt to invoke a full build. Instead, after code changes, emit the G4 summary and hand off with: "请编译验证。"
- CMake-only changes (new source files) require the user to regenerate the MSVC project. The subagent MUST note this in the G4 summary.

### G6. Smoke test form

All testing is manual smoke (no gtest). Each UI task ends with a smoke directive naming:
- which permission to toggle in the backend DB
- what to observe in the client within 60 s
- what to verify is still working (regression check)

Wait for the user's smoke report before committing the task.

### G7. Commit messages

Follow Phase 1 precedent: `feat(employee): <terse imperative>`. Hook trailer:
```
Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

### G8. Session access pattern

Every UI site needs a `not_null<Main::Session*>` to pass into the guard. Common access patterns in tdesktop:
- Controllers: `&_controller->session()` or `&controller->session()`
- Peer-based sites: `&peer->session()` or `&user->session()`
- History widgets: `&history->session()` or `&_history->session()`
- Inside a `ComposeControls`: `&session` (member) or `&_session`

The implementer must locate the nearest existing session accessor in each file before inserting the guard.

---

## File Manifest

### New files (2)

1. `Telegram/SourceFiles/intro/employee/employee_ui_guard.h` — 3 free function declarations
2. `Telegram/SourceFiles/intro/employee/employee_ui_guard.cpp` — implementations

### Modified files — core (3)

3. `Telegram/CMakeLists.txt` — register the 2 new files
4. `Telegram/SourceFiles/main/main_account.h` — add timer, failure counter, 3 private methods
5. `Telegram/SourceFiles/main/main_account.cpp` — implement timer + wire into 3 existing methods

### Modified files — UI (~30, grouped by task)

Listed per-task below. Each file change is a 1-3 line insertion plus one gated `#include`.

---

## Task 1: Helper layer + CMake registration

**Files:**
- Create: `Telegram/SourceFiles/intro/employee/employee_ui_guard.h`
- Create: `Telegram/SourceFiles/intro/employee/employee_ui_guard.cpp`
- Modify: `Telegram/CMakeLists.txt`

### Step 1: Context analysis

- [ ] Grep existing Phase 1 headers for license header text, include order, namespace style:

```bash
head -20 Telegram/SourceFiles/intro/employee/employee_permissions.h
head -20 Telegram/SourceFiles/intro/employee/employee_auth.h
```

Expected: copyright block, then `#pragma once`, then `#ifdef TDESKTOP_EMPLOYEE_MODE`.

- [ ] Confirm Phase 1 `Permissions` API surface compiles against what we'll call:

```bash
grep -n "has\|value\|authorized\|apply\|clear\|token" Telegram/SourceFiles/intro/employee/employee_permissions.h
```

Expected methods: `has(key)`, `value(key)`, `authorized()`, `token()`, `apply(values, token)`, `clear()`.

### Step 2: Create `employee_ui_guard.h`

- [ ] Write the file with these exact contents:

```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/required.h"
#include "intro/employee/employee_permissions.h"

class QAction;

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Intro::Employee {

// One-shot: read the current permission value and set enabled state on
// a QAction. Use for short-lived widgets (menu items, transient Box buttons)
// that are rebuilt on each user interaction.
void GuardAction(
	not_null<Main::Session*> session,
	PermissionKey key,
	not_null<QAction*> action);

// Reactive: subscribe the widget's enabled state to permission changes.
// False -> setDisabled(true) + WA_TransparentForMouseEvents=true.
// Subscription attached to widget->lifetime(); auto-released on destruction.
// Use for long-lived widgets (compose controls, info-page action buttons).
void BindDisabled(
	not_null<Main::Session*> session,
	PermissionKey key,
	not_null<Ui::RpWidget*> widget);

// Pure read. Use in callback early-return for non-button entry points:
// Enter key, Delete/Backspace, drag-drop, paste, keyboard shortcuts.
[[nodiscard]] bool Allowed(
	not_null<Main::Session*> session,
	PermissionKey key);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

### Step 3: Create `employee_ui_guard.cpp`

- [ ] Write the file with these exact contents:

```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "intro/employee/employee_ui_guard.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/rp_widget.h"

namespace Intro::Employee {

void GuardAction(
		not_null<Main::Session*> session,
		PermissionKey key,
		not_null<QAction*> action) {
	action->setEnabled(session->account().employeePermissions().has(key));
}

void BindDisabled(
		not_null<Main::Session*> session,
		PermissionKey key,
		not_null<Ui::RpWidget*> widget) {
	session->account().employeePermissions().value(
		key
	) | rpl::start_with_next([raw = widget.get()](bool allowed) {
		raw->setDisabled(!allowed);
		raw->setAttribute(
			Qt::WA_TransparentForMouseEvents,
			!allowed);
	}, widget->lifetime());
}

bool Allowed(
		not_null<Main::Session*> session,
		PermissionKey key) {
	return session->account().employeePermissions().has(key);
}

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

### Step 4: Register in CMake

- [ ] Open `Telegram/CMakeLists.txt`. Locate the existing `intro/employee/` block (from Phase 1):

```bash
grep -n "intro/employee/" Telegram/CMakeLists.txt
```

- [ ] Insert the two new entries **alphabetically** within that block:

```
intro/employee/employee_ui_guard.cpp
intro/employee/employee_ui_guard.h
```

The block should end with these two lines (alphabetically, `employee_ui_guard` comes after `employee_verify`).

### Step 5: Chinese bullet summary + hand off to user for compile

- [ ] Emit:

```
编译前总结（Task 1）：
- 新增 intro/employee/employee_ui_guard.h（3 个自由函数声明，全 #ifdef 包裹）
- 新增 intro/employee/employee_ui_guard.cpp（GuardAction / BindDisabled / Allowed 实现）
- CMakeLists.txt 注册上述两个新文件
- 未触及任何 UI / Main::Account 代码
- 注意：新文件入 CMake，请先重新 configure MSVC 工程再编译
请编译验证。
```

Wait for "编译通过" before proceeding.

### Step 6: Commit

- [ ] Commit once user confirms build succeeds:

```bash
git add Telegram/SourceFiles/intro/employee/employee_ui_guard.h \
        Telegram/SourceFiles/intro/employee/employee_ui_guard.cpp \
        Telegram/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(employee): add UI guard helpers

GuardAction / BindDisabled / Allowed — minimal 3-function layer used
at each UI site to disable controls when a permission is false.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Periodic verify timer

**Files:**
- Modify: `Telegram/SourceFiles/main/main_account.h`
- Modify: `Telegram/SourceFiles/main/main_account.cpp`

### Step 1: Context analysis

- [ ] Read the existing Phase 1 Employee region to locate insertion points:

```bash
grep -n "_employeePermissions\|_employeeVerify\|_employeeBackend\|startEmployeeVerify\|handleEmployeeAuthMissingOrCorrupt\|kickOffEmployeeVerifyIfAuthorized" \
     Telegram/SourceFiles/main/main_account.h \
     Telegram/SourceFiles/main/main_account.cpp | head -30
```

Expected: Phase 1 has `_employeePermissions`, `_employeeVerify`, `_employeeBackend` as private members; `kickOffEmployeeVerifyIfAuthorized`, `onEmployeeVerifyInvalidToken`, `handleEmployeeAuthMissingOrCorrupt` as private methods; `applyEmployeeBootstrap`, `applyEmployeeReset`, `employeePermissions()` as public.

- [ ] Confirm `base::Timer` idiom:

```bash
grep -B1 -A2 "_updateNotifyTimer" Telegram/SourceFiles/apiwrap.cpp | head -12
```

Expected: constructor-list initialisation of the form `, _timer([=] { callback(); })`.

- [ ] Confirm `base::RandomValue` for jitter:

```bash
grep -n "base::RandomValue" Telegram/SourceFiles/apiwrap.cpp | head -3
```

Expected: `base::RandomValue<uint64>()` (or similar) — we'll use `<int>` and modulo.

### Step 2: Modify `main_account.h`

- [ ] Locate the existing `#ifdef TDESKTOP_EMPLOYEE_MODE` private region (around the lines that declare `_employeePermissions` / `_employeeVerify`).

- [ ] Add the three new method declarations immediately AFTER `void handleEmployeeAuthMissingOrCorrupt();`:

```cpp
void startEmployeeVerifyTimer();
void stopEmployeeVerifyTimer();
void onEmployeeVerifyTimerTick();
```

- [ ] Add the two new members immediately AFTER `Intro::Employee::BackendType _employeeBackend = ...;`:

```cpp
base::Timer _employeeVerifyTimer;
int _employeeVerifyConsecutiveFailures = 0;
```

- [ ] If `main_account.h` does not already `#include "base/timer.h"`, verify inclusion (it likely already comes via PCH or an existing include — grep first):

```bash
grep -n "base/timer.h" Telegram/SourceFiles/main/main_account.h
```

If not present AND not coming via PCH transitively, add `#include "base/timer.h"` inside the existing `#ifdef TDESKTOP_EMPLOYEE_MODE` include block near the top.

### Step 3: Modify `main_account.cpp` constructor

- [ ] Find the existing `#ifdef TDESKTOP_EMPLOYEE_MODE` constructor-init block (where `_employeePermissions` and `_employeeVerify` are `std::make_unique`'d). Add at the end of that block:

```cpp
_employeeVerifyTimer.setCallback([=] {
    onEmployeeVerifyTimerTick();
});
```

(We use `setCallback` rather than constructor-init list because the existing Phase 1 block uses imperative `make_unique` assignments — staying consistent.)

### Step 4: Modify `kickOffEmployeeVerifyIfAuthorized` — add timer start to non-401 paths

- [ ] Locate the Phase 1 callback at approx. line 767. The structure is:

```cpp
crl::guard(this, [this](Intro::Employee::VerifyResult result) {
    if (const auto s = std::get_if<Intro::Employee::VerifySuccess>(&result)) {
        // ... apply + persist ...
        return;
    }
    const auto f = std::get_if<Intro::Employee::VerifyFailure>(&result);
    Assert(f != nullptr);
    switch (f->kind) {
    case Intro::Employee::VerifyFailure::Kind::InvalidToken:
        onEmployeeVerifyInvalidToken();
        return;
    case Intro::Employee::VerifyFailure::Kind::Network:
    case Intro::Employee::VerifyFailure::Kind::Server:
    case Intro::Employee::VerifyFailure::Kind::BadJson:
        LOG(("Employee: verify failed kind=%1; keeping disk state").arg(int(f->kind)));
        return;
    }
})
```

- [ ] Insert `startEmployeeVerifyTimer();` immediately before the `return;` on the Success branch. Insert it also immediately before the `return;` on the Network/Server/BadJson branch. Do NOT insert it on the InvalidToken branch (timer never starts; `onEmployeeVerifyInvalidToken` triggers logout).

Result (Success branch shown; same pattern for failure branches):

```cpp
if (const auto s = std::get_if<Intro::Employee::VerifySuccess>(&result)) {
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
    startEmployeeVerifyTimer();  // <-- ADDED
    return;
}
```

### Step 5: Modify `applyEmployeeBootstrap`

- [ ] At the end of `Account::applyEmployeeBootstrap(...)` (after the `startMtp` scope at ~line 724), add:

```cpp
startEmployeeVerifyTimer();
```

### Step 6: Modify `applyEmployeeReset`

- [ ] At the very top of `Account::applyEmployeeReset()` (before `if (_employeeVerify) { _employeeVerify->cancel(); }`), add:

```cpp
stopEmployeeVerifyTimer();
```

### Step 7: Implement the three new methods

- [ ] Append to `main_account.cpp` (inside the existing `#ifdef TDESKTOP_EMPLOYEE_MODE` section that contains `handleEmployeeAuthMissingOrCorrupt`):

```cpp
void Account::startEmployeeVerifyTimer() {
    _employeeVerifyTimer.cancel();
    constexpr auto kBaseMs = crl::time(60 * 1000);
    constexpr auto kJitterMaxMs = crl::time(10 * 1000);
    const auto jitter = crl::time(
        base::RandomValue<int>() % int(kJitterMaxMs + 1));
    // Clamp negatives in case RandomValue returns a negative int.
    const auto safeJitter = (jitter < 0) ? -jitter : jitter;
    _employeeVerifyTimer.callOnce(kBaseMs + safeJitter);
}

void Account::stopEmployeeVerifyTimer() {
    _employeeVerifyTimer.cancel();
    _employeeVerifyConsecutiveFailures = 0;
}

void Account::onEmployeeVerifyTimerTick() {
    if (!_employeePermissions || !_employeePermissions->authorized()) {
        return;
    }
    const auto token = _employeePermissions->token();
    _employeeVerify->verify(
        _employeeBackend,
        token,
        crl::guard(this, [this, token](
                Intro::Employee::VerifyResult result) {
            if (const auto s = std::get_if<
                    Intro::Employee::VerifySuccess>(&result)) {
                if (_employeeVerifyConsecutiveFailures > 0) {
                    LOG(("Employee: verify recovered after %1 failures"
                        ).arg(_employeeVerifyConsecutiveFailures));
                    _employeeVerifyConsecutiveFailures = 0;
                }
                _employeePermissions->apply(s->permissions, token);
                local().writeEmployeeAuth(
                    Intro::Employee::SerializeAuthSnapshot(
                        Intro::Employee::AuthSnapshot{
                            .token = token,
                            .permissions = s->permissions,
                            .backend = _employeeBackend,
                        }));
                _employeeVerifyTimer.callOnce(60 * 1000);
                return;
            }
            const auto f = std::get_if<
                Intro::Employee::VerifyFailure>(&result);
            Assert(f != nullptr);
            if (f->kind == Intro::Employee::VerifyFailure::Kind::InvalidToken) {
                stopEmployeeVerifyTimer();
                onEmployeeVerifyInvalidToken();
                return;
            }
            ++_employeeVerifyConsecutiveFailures;
            const auto n = _employeeVerifyConsecutiveFailures;
            if (n == 1 || (n % 10) == 0) {
                LOG(("Employee: verify tick failed kind=%1 count=%2"
                    ).arg(int(f->kind)).arg(n));
            }
            _employeeVerifyTimer.callOnce(60 * 1000);
        }));
}
```

### Step 8: Chinese bullet summary + compile handoff

- [ ] Emit:

```
编译前总结（Task 2）：
- Main::Account 加 3 个私有方法（startEmployeeVerifyTimer / stopEmployeeVerifyTimer / onEmployeeVerifyTimerTick）
- 加 2 个成员：base::Timer _employeeVerifyTimer、int _employeeVerifyConsecutiveFailures
- 构造函数注册 timer 回调
- 在 kickOffEmployeeVerifyIfAuthorized 的 Success + Network/Server/BadJson 两路分支 return 前插入 startEmployeeVerifyTimer()（401 分支不插）
- applyEmployeeBootstrap 末尾追加 startEmployeeVerifyTimer()
- applyEmployeeReset 顶部追加 stopEmployeeVerifyTimer()
- 未改任何 UI
请编译验证。
```

### Step 9: Smoke test directive

- [ ] Ask user to smoke:

1. 启动客户端，观察日志首次出现 `Employee: verify ok`（冷启动）。
2. 等 60-70 秒，后端日志应出现 1 次 `/api/auth/verify` 请求（定时 tick 触发）。
3. 关掉后端服务；等约 2 分钟；客户端日志应出现 1 条 `Employee: verify tick failed kind=0 count=1`；再等 10 分钟应再出现 1 条 `count=10`；中间无日志。
4. 重启后端；下一次 tick 成功后应出现 1 条 `Employee: verify recovered after N failures`。
5. 手动让后端把 token 列为失效；下一次 tick 应触发硬登出回到 Intro 页。

### Step 10: Commit

- [ ] After smoke report:

```bash
git add Telegram/SourceFiles/main/main_account.h \
        Telegram/SourceFiles/main/main_account.cpp
git commit -m "$(cat <<'EOF'
feat(employee): periodic verify timer with jitter + log throttling

Adds a 60s base::Timer in Main::Account that reuses VerifyClient.
First tick carries 0-10s jitter. 401 -> forced logout via existing
Phase 1 path. Network/Server/BadJson failures keep disk state; logs
are throttled to 1st, 10th, 20th, ... and a recovery line.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## UI Task Recipes (used by Tasks 3-9)

### Recipe: Adding `GuardAction` to a QAction-returning `addAction` call

Tdesktop's `QMenu::addAction` and `Ui::Menu::Menu::addAction` both return a `QAction*`. If the existing code discards the return value, capture it:

```cpp
// BEFORE:
menu->addAction(tr::lng_example(tr::now), [=] { doThing(); });

// AFTER:
const auto action = menu->addAction(tr::lng_example(tr::now), [=] { doThing(); });
#ifdef TDESKTOP_EMPLOYEE_MODE
Intro::Employee::GuardAction(
    session, // or &peer->session() — use what the file uses nearby
    Intro::Employee::PermissionKey::KeyName,
    action);
#endif
```

If the file does not yet include the helper, add at the top (after the existing `#include "..."` block):

```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
#include "intro/employee/employee_ui_guard.h"
#endif
```

### Recipe: Adding `BindDisabled` to a long-lived widget

```cpp
// Immediately after the widget is created and populated with its handlers:
#ifdef TDESKTOP_EMPLOYEE_MODE
Intro::Employee::BindDisabled(
    session,
    Intro::Employee::PermissionKey::KeyName,
    button);
#endif
```

If `button` is a `std::unique_ptr<Ui::RpWidget>` or `object_ptr<...>`, pass `button.data()` or `button.get()`. The helper takes `not_null<Ui::RpWidget*>`.

### Recipe: Callback early-return for non-button inputs

```cpp
// At the very top of a handler (Enter key, Del key, drag-drop drop, shortcut, etc.):
#ifdef TDESKTOP_EMPLOYEE_MODE
if (!Intro::Employee::Allowed(
        session,
        Intro::Employee::PermissionKey::KeyName)) {
    return;
}
#endif
```

For handlers that return a value (e.g., `bool`), pick the semantically-safe return (`false` for "did not consume event", matching Qt conventions):

```cpp
if (!Intro::Employee::Allowed(session, Intro::Employee::PermissionKey::KeyName)) {
    return false;
}
```

---

## Task 3: msg.edit + msg.forward

Small, centralised. 5 sites across 3 files.

**Files:**
- Modify: `Telegram/SourceFiles/history/view/history_view_context_menu.cpp`
- Modify: `Telegram/SourceFiles/history/history_widget.cpp`
- Modify: `Telegram/SourceFiles/history/view/history_view_compose_controls.cpp`

### Step 1: Context analysis

- [ ] Grep anchors:

```bash
grep -n "tr::lng_context_edit_msg\|tr::lng_context_forward_msg\|tr::lng_context_forward_selected" \
     Telegram/SourceFiles/history/view/history_view_context_menu.cpp

grep -n "editMessage\b\|_field->upRequests\|upRequests" \
     Telegram/SourceFiles/history/history_widget.cpp \
     Telegram/SourceFiles/history/view/history_view_compose_controls.cpp | head -20
```

- [ ] Note which nearby variable carries the session: `_history->session()`, `peer->session()`, or `&session`. Use that in the guard calls.

### Step 2: Site insertions

Each row = one edit; follow the matching recipe.

| # | File | Anchor | Guard | Permission | Notes |
|---|------|--------|-------|------------|-------|
| 3.1 | `history_view_context_menu.cpp` | `tr::lng_context_edit_msg` | GuardAction | `MsgEdit` | Capture `auto action = menu->addAction(...)`; session via the item's `history()->session()`. |
| 3.2 | `history_view_context_menu.cpp` | `tr::lng_context_forward_msg` (single-message forward) | GuardAction | `MsgForward` | Same file; same session accessor. |
| 3.3 | `history_view_context_menu.cpp` | `tr::lng_context_forward_selected` (or the action that forwards selected items) | GuardAction | `MsgForward` | Grep for "forward_selected"; if absent, use the second `forward`-related `addAction` in the file. |
| 3.4 | `history_widget.cpp` | the `editLastMessage()` / `_field` Up-key handler (around the `upRequests` / `editLastMessage` call) | Allowed early return | `MsgEdit` | Top of the handler that fires on Up key with empty input. |
| 3.5 | `history_view_compose_controls.cpp` | the `ComposeControls::editLastMessage` path / `_field->upRequests` subscription | Allowed early return | `MsgEdit` | Top of the lambda body. |

### Step 3: Add gated include to each modified file (if not already present)

- [ ] In each of the three files, near the other `#include "..."` lines, insert:

```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
#include "intro/employee/employee_ui_guard.h"
#endif
```

### Step 4: Chinese bullet summary + compile handoff

- [ ] Emit:

```
编译前总结（Task 3：msg.edit + msg.forward）：
- history_view_context_menu.cpp：右键 "编辑消息" / "转发" / "转发选中" 三处 addAction 的 QAction 捕获后加 GuardAction
- history_widget.cpp：↑ 键 editLastMessage 回调开头加 Allowed 早退
- history_view_compose_controls.cpp：ComposeControls 的 ↑ 键编辑入口加 Allowed 早退
- 3 个文件各加 1 个 gated include
请编译验证。
```

### Step 5: Smoke test directive

- [ ] Ask user to smoke:

1. 全权限：右键任一己发消息，应看到"编辑"和"转发"可点。
2. 撤销 msg.edit；60 s 内关掉再重开右键菜单，"编辑"灰；输入框空时按 ↑ 无反应。
3. 撤销 msg.forward；右键"转发"灰。
4. 撤销二者同时；上述两条均生效。

### Step 6: Commit

- [ ] After smoke confirms:

```bash
git add Telegram/SourceFiles/history/view/history_view_context_menu.cpp \
        Telegram/SourceFiles/history/history_widget.cpp \
        Telegram/SourceFiles/history/view/history_view_compose_controls.cpp
git commit -m "$(cat <<'EOF'
feat(employee): disable msg.edit and msg.forward entry points

Context menu items (Edit / Forward / Forward Selected) gated via
GuardAction. Up-key edit-last paths gated via Allowed() early return.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: group.* (create + delete + addMember + removeMember)

11 sites across 7 files. All group/channel lifecycle actions in one commit.

**Files:**
- Modify: `Telegram/SourceFiles/window/window_main_menu.cpp`
- Modify: `Telegram/SourceFiles/window/window_session_controller.cpp`
- Modify: `Telegram/SourceFiles/window/window_peer_menu.cpp`
- Modify: `Telegram/SourceFiles/info/profile/info_profile_actions.cpp`
- Modify: `Telegram/SourceFiles/info/profile/info_profile_members.cpp`
- Modify: `Telegram/SourceFiles/boxes/peers/edit_participants_box.cpp`

### Step 1: Context analysis

- [ ] Grep all anchors in one pass:

```bash
grep -n "tr::lng_create_group_title\|tr::lng_create_channel_title\|showNewGroup\|showNewChannel" \
     Telegram/SourceFiles/window/window_main_menu.cpp \
     Telegram/SourceFiles/window/window_session_controller.cpp

grep -n "addDeleteChat\|addLeaveChat\|addNewMembers\|PeerMenuAddChannelMembers\|AddChatMembers" \
     Telegram/SourceFiles/window/window_peer_menu.cpp

grep -n "addLeaveChannelAction\|SetupAddChannelMember" \
     Telegram/SourceFiles/info/profile/info_profile_actions.cpp

grep -n "addMember\|setupButtons\|setupHeader" \
     Telegram/SourceFiles/info/profile/info_profile_members.cpp

grep -n "rowContextMenu\|kickParticipant\|showRestricted\|showAdmin" \
     Telegram/SourceFiles/boxes/peers/edit_participants_box.cpp
```

- [ ] Read nearby lines to find the session accessor in each file (usually `&controller->session()` in window_*; `&peer->session()` in peer-based sites; `_controller->session()` in Info pages; `&_peer->session()` in edit_participants_box).

### Step 2: Site insertions

| # | File | Anchor | Guard | Permission |
|---|------|--------|-------|------------|
| 4.1 | `window_main_menu.cpp` | `tr::lng_create_group_title` addAction (or the "New Group" menu entry) | GuardAction | `GroupCreate` |
| 4.2 | `window_main_menu.cpp` | "New Channel" entry (nearby; same style) | GuardAction | `GroupCreate` |
| 4.3 | `window_session_controller.cpp` | top of `showNewGroup()` AND top of `showNewChannel()` | Allowed early return | `GroupCreate` (safety net; `return;` since methods are `void`) |
| 4.4 | `window_peer_menu.cpp` | body of `Filler::addDeleteChat` — the `addAction(tr::lng_profile_delete_conversation / tr::lng_profile_clear_and_exit / ...)` | GuardAction | `GroupDelete` |
| 4.5 | `window_peer_menu.cpp` | body of `Filler::addLeaveChat` — `addAction(tr::lng_profile_leave_channel / tr::lng_profile_leave_group)` | GuardAction | `GroupDelete` |
| 4.6 | `info/profile/info_profile_actions.cpp` | `ActionsFiller::addLeaveChannelAction` — its returned/created button (grep for `AddActionButton` / `SettingsButton::create`) | BindDisabled | `GroupDelete` |
| 4.7 | `info/profile/info_profile_members.cpp` | `Members::setupHeader` or `Members::setupButtons` — the "add member" icon button | BindDisabled | `GroupAddMember` |
| 4.8 | `info/profile/info_profile_actions.cpp` | `SetupAddChannelMember` — the created "Add member" button | BindDisabled | `GroupAddMember` |
| 4.9 | `window_peer_menu.cpp` | `Filler::addNewMembers` — the `addAction(tr::lng_profile_add_participant / tr::lng_channel_add_users)` | GuardAction | `GroupAddMember` |
| 4.10 | `edit_participants_box.cpp` | `ParticipantsBoxController::rowContextMenu` — the Kick entry (around `tr::lng_context_remove_from_group` / `tr::lng_profile_kick`) | GuardAction | `GroupRemoveMember` |
| 4.11 | `edit_participants_box.cpp` | same function — Restrict entry (`tr::lng_channel_ban_user`) | GuardAction | `GroupRemoveMember` |
| 4.12 | `edit_participants_box.cpp` | same function — Edit Admin entry (`tr::lng_context_edit_permissions` / `tr::lng_context_promote_admin`) | GuardAction | `GroupRemoveMember` |

### Step 3: Add gated include to each modified file

- [ ] 6 files total. Each gets the gated include block at the top.

### Step 4: Chinese bullet summary

- [ ] Emit:

```
编译前总结（Task 4：group.*）：
- window_main_menu.cpp：主菜单"新建群组" / "新建频道"两处 GuardAction(GroupCreate)
- window_session_controller.cpp：showNewGroup / showNewChannel 顶部 Allowed(GroupCreate) 早退
- window_peer_menu.cpp：addDeleteChat / addLeaveChat 两处 GuardAction(GroupDelete)；addNewMembers 一处 GuardAction(GroupAddMember)
- info_profile_actions.cpp：Leave Channel 按钮 BindDisabled(GroupDelete)；Add Member 按钮 BindDisabled(GroupAddMember)
- info_profile_members.cpp：加号按钮 BindDisabled(GroupAddMember)
- edit_participants_box.cpp：rowContextMenu 里 Kick/Restrict/Edit Admin 三项 GuardAction(GroupRemoveMember)
- 6 个文件各加 gated include
请编译验证。
```

### Step 5: Smoke test directive

- [ ] Ask user:

1. 撤销 `GroupCreate` → 主菜单"新建群组 / 新建频道"灰；`window_session_controller` 的 showNewGroup 被触发时 return。
2. 撤销 `GroupDelete` → 会话列表右键"删除聊天 / 退出群组"灰；info 页"退出频道"灰。
3. 撤销 `GroupAddMember` → 群成员页加号灰；右键"添加成员"灰；info 页"添加成员"灰。
4. 撤销 `GroupRemoveMember` → 成员管理页右键某成员的 Kick/Restrict/Edit admin 三项全灰。

### Step 6: Commit

- [ ] After smoke:

```bash
git add Telegram/SourceFiles/window/window_main_menu.cpp \
        Telegram/SourceFiles/window/window_session_controller.cpp \
        Telegram/SourceFiles/window/window_peer_menu.cpp \
        Telegram/SourceFiles/info/profile/info_profile_actions.cpp \
        Telegram/SourceFiles/info/profile/info_profile_members.cpp \
        Telegram/SourceFiles/boxes/peers/edit_participants_box.cpp
git commit -m "$(cat <<'EOF'
feat(employee): disable group.* entry points

GroupCreate / GroupDelete / GroupAddMember / GroupRemoveMember gated
at main menu, session controller entry points, peer-menu entries,
info-page buttons, and participant row context menu.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: contact.* (add + block + editNote)

17 sites across 9 files. Densest of the UI tasks.

**Files:**
- Modify: `Telegram/SourceFiles/info/profile/info_profile_actions.cpp`
- Modify: `Telegram/SourceFiles/window/window_peer_menu.cpp`
- Modify: `Telegram/SourceFiles/window/window_main_menu.cpp`
- Modify: `Telegram/SourceFiles/boxes/peer_list_controllers.cpp`
- Modify: `Telegram/SourceFiles/core/phone_click_handler.cpp`
- Modify: `Telegram/SourceFiles/history/view/media/history_view_contact.cpp`
- Modify: `Telegram/SourceFiles/history/view/history_view_contact_status.cpp`
- Modify: `Telegram/SourceFiles/history/history_inner_widget.cpp`
- Modify: `Telegram/SourceFiles/settings/sections/settings_blocked_peers.cpp`

Note: `window_peer_menu.cpp` and `info_profile_actions.cpp` may already have the gated include from Task 4. Don't add it twice.

### Step 1: Context analysis

- [ ] Grep anchors for all three permissions:

```bash
# contact.add
grep -n "tr::lng_info_add_as_contact\|tr::lng_menu_add_contact\|tr::lng_menu_new_contact\|tr::lng_contacts_add\|AddContactBox\|EditContactBox.*create\|tr::lng_context_view_contact_add" \
     Telegram/SourceFiles/info/profile/info_profile_actions.cpp \
     Telegram/SourceFiles/window/window_peer_menu.cpp \
     Telegram/SourceFiles/window/window_main_menu.cpp \
     Telegram/SourceFiles/boxes/peer_list_controllers.cpp \
     Telegram/SourceFiles/core/phone_click_handler.cpp \
     Telegram/SourceFiles/history/view/media/history_view_contact.cpp \
     Telegram/SourceFiles/history/view/history_view_contact_status.cpp

# contact.block
grep -n "tr::lng_blocked_list_add\|tr::lng_profile_block_user\|tr::lng_context_block\|addBlockUser\|addBlockAction" \
     Telegram/SourceFiles/info/profile/info_profile_actions.cpp \
     Telegram/SourceFiles/window/window_peer_menu.cpp \
     Telegram/SourceFiles/history/history_inner_widget.cpp \
     Telegram/SourceFiles/history/view/history_view_contact_status.cpp \
     Telegram/SourceFiles/settings/sections/settings_blocked_peers.cpp

# contact.editNote
grep -n "tr::lng_info_edit_contact\|tr::lng_menu_edit_contact\|tr::lng_context_edit_contact" \
     Telegram/SourceFiles/info/profile/info_profile_actions.cpp \
     Telegram/SourceFiles/window/window_peer_menu.cpp
```

### Step 2: Site insertions

**contact.add:**

| # | File | Anchor | Guard | Permission |
|---|------|--------|-------|------------|
| 5.1 | `info_profile_actions.cpp` | `tr::lng_info_add_as_contact` (Add to Contacts button) | BindDisabled | `ContactAdd` |
| 5.2 | `window_peer_menu.cpp` | `tr::lng_info_add_as_contact` / `tr::lng_menu_add_contact` addAction | GuardAction | `ContactAdd` |
| 5.3 | `window_main_menu.cpp` | `tr::lng_menu_new_contact` addAction | GuardAction | `ContactAdd` |
| 5.4 | `boxes/peer_list_controllers.cpp` | the "Add Contact" FAB / bottom button in Contacts box | BindDisabled | `ContactAdd` |
| 5.5 | `core/phone_click_handler.cpp` | `tr::lng_context_view_contact_add` (or the "add contact" entry in the phone context menu) | GuardAction | `ContactAdd` |
| 5.6 | `history/view/media/history_view_contact.cpp` | the "Add to Contacts" button rendered on a received contact card | BindDisabled | `ContactAdd` |
| 5.7 | `history/view/history_view_contact_status.cpp` | top-bar "Add" button in private chats | BindDisabled | `ContactAdd` |

**contact.block:**

| # | File | Anchor | Guard | Permission |
|---|------|--------|-------|------------|
| 5.8 | `info_profile_actions.cpp` | `tr::lng_profile_block_user` (Block button) | BindDisabled | `ContactBlock` |
| 5.9 | `window_peer_menu.cpp` | first `addBlockUser` (profile menu path) | GuardAction | `ContactBlock` |
| 5.10 | `window_peer_menu.cpp` | second `addBlockUser` (context menu path) | GuardAction | `ContactBlock` |
| 5.11 | `history/history_inner_widget.cpp` | Block entry in sender right-click (around `tr::lng_context_block`) | GuardAction | `ContactBlock` |
| 5.12 | `history/history_inner_widget.cpp` | Block entry in forward-stamp right-click (second occurrence) | GuardAction | `ContactBlock` |
| 5.13 | `history/view/history_view_contact_status.cpp` | top-bar "Block" button | BindDisabled | `ContactBlock` |
| 5.14 | `settings/sections/settings_blocked_peers.cpp` | "Add" button in Blocked list | BindDisabled | `ContactBlock` |

**contact.editNote:**

| # | File | Anchor | Guard | Permission |
|---|------|--------|-------|------------|
| 5.15 | `info_profile_actions.cpp` | `tr::lng_info_edit_contact` (Edit Contact button) | BindDisabled | `ContactEditNote` |
| 5.16 | `window_peer_menu.cpp` | `tr::lng_menu_edit_contact` addAction | GuardAction | `ContactEditNote` |

### Step 3: Add gated include to each modified file

- [ ] 9 files total (skip any already-included from Task 4).

### Step 4: Chinese bullet summary

- [ ] Emit:

```
编译前总结（Task 5：contact.*）：
- info_profile_actions.cpp：Add/Block/EditContact 三个按钮 BindDisabled（ContactAdd/Block/EditNote）
- window_peer_menu.cpp：Add/EditContact 菜单 GuardAction；BlockUser 两处 GuardAction
- window_main_menu.cpp：新建联系人 GuardAction(ContactAdd)
- peer_list_controllers.cpp：联系人 Box 底部"添加"按钮 BindDisabled
- phone_click_handler.cpp：电话号右键"添加联系人" GuardAction
- history_view_contact.cpp：联系人卡片"添加"按钮 BindDisabled
- history_view_contact_status.cpp：顶栏 Add/Block 两个按钮 BindDisabled
- history_inner_widget.cpp：sender/forward 右键 Block 两处 GuardAction
- settings_blocked_peers.cpp：黑名单"添加"按钮 BindDisabled
- 9 个文件 gated include（其中 2 个可能已从 Task 4 引入）
请编译验证。
```

### Step 5: Smoke test directive

- [ ] Ask user:

1. 撤销 `ContactAdd` → 所有 7 个"添加联系人"入口灰或菜单项灰。
2. 撤销 `ContactBlock` → info 页、菜单、消息右键、顶栏、设置页所有"屏蔽"入口灰。
3. 撤销 `ContactEditNote` → info 页和菜单的"编辑联系人"两处灰。
4. 全开 → 所有上述入口恢复可点。

### Step 6: Commit

- [ ] After smoke:

```bash
git add Telegram/SourceFiles/info/profile/info_profile_actions.cpp \
        Telegram/SourceFiles/window/window_peer_menu.cpp \
        Telegram/SourceFiles/window/window_main_menu.cpp \
        Telegram/SourceFiles/boxes/peer_list_controllers.cpp \
        Telegram/SourceFiles/core/phone_click_handler.cpp \
        Telegram/SourceFiles/history/view/media/history_view_contact.cpp \
        Telegram/SourceFiles/history/view/history_view_contact_status.cpp \
        Telegram/SourceFiles/history/history_inner_widget.cpp \
        Telegram/SourceFiles/settings/sections/settings_blocked_peers.cpp
git commit -m "$(cat <<'EOF'
feat(employee): disable contact.* entry points

ContactAdd / ContactBlock / ContactEditNote gated across info-page
buttons, peer-menu items, main menu, contacts Box, phone handler,
contact card, chat topbar, message right-click, and settings.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: folder.edit + folder.addChat + ui.disableMentionTooltip

9 sites across 6 files.

**Files:**
- Modify: `Telegram/SourceFiles/settings/sections/settings_folders.cpp`
- Modify: `Telegram/SourceFiles/ui/widgets/chat_filters_tabs_strip.cpp`
- Modify: `Telegram/SourceFiles/window/window_filters_menu.cpp`
- Modify: `Telegram/SourceFiles/boxes/choose_filter_box.cpp`
- Modify: `Telegram/SourceFiles/chat_helpers/field_autocomplete.cpp`

### Step 1: Context analysis

- [ ] Grep:

```bash
grep -n "tr::lng_filters_create\|tr::lng_filters_context_edit\|tr::lng_filters_context_remove\|tr::lng_filters_edit\|tr::lng_filters_remove" \
     Telegram/SourceFiles/settings/sections/settings_folders.cpp \
     Telegram/SourceFiles/ui/widgets/chat_filters_tabs_strip.cpp \
     Telegram/SourceFiles/window/window_filters_menu.cpp

grep -n "FillChooseFilterMenu\|validator.add" Telegram/SourceFiles/boxes/choose_filter_box.cpp
grep -n "InitFieldAutocomplete\|parsed.query" Telegram/SourceFiles/chat_helpers/field_autocomplete.cpp | head -20
```

### Step 2: Site insertions

**folder.edit:**

| # | File | Anchor | Guard | Permission |
|---|------|--------|-------|------------|
| 6.1 | `settings_folders.cpp` | "Create" filter button | GuardAction (if it's a QAction) or BindDisabled (if `Ui::SettingsButton` — check) | `FolderEdit` |
| 6.2 | `settings_folders.cpp` | per-row "edit" action | GuardAction | `FolderEdit` |
| 6.3 | `settings_folders.cpp` | per-row "remove" action | GuardAction | `FolderEdit` |
| 6.4 | `chat_filters_tabs_strip.cpp` | tab right-click "Edit" | GuardAction | `FolderEdit` |
| 6.5 | `chat_filters_tabs_strip.cpp` | tab right-click "Remove" | GuardAction | `FolderEdit` |
| 6.6 | `window_filters_menu.cpp` | sidebar filter "Edit" | GuardAction | `FolderEdit` |
| 6.7 | `window_filters_menu.cpp` | sidebar filter "Remove" | GuardAction | `FolderEdit` |

**folder.addChat:** (1 funnel, multi-action)

| # | File | Anchor | Guard | Permission |
|---|------|--------|-------|------------|
| 6.8 | `choose_filter_box.cpp` | inside `FillChooseFilterMenu`, the `menu->addAction(...)` that generates per-filter entries | GuardAction (loop: apply to each generated QAction) | `FolderAddChat` |
| 6.9 | `choose_filter_box.cpp` | inside `validator.add()` callback (or equivalent post-click handler) | Allowed early return | `FolderAddChat` |

**ui.disableMentionTooltip:** (inverted semantic — use raw `has()`, NOT `Allowed()`)

| # | File | Anchor | Guard | Permission |
|---|------|--------|-------|------------|
| 6.10 | `field_autocomplete.cpp` | inside `InitFieldAutocomplete`, the `check` lambda, immediately before the `@` branch that calls `raw->showFiltered(...)` | Raw `has()` check | `UiDisableMentionTooltip` |

For site 6.10, insert this exact block (note the semantic comment — the key's `has()==true` *suppresses*):

```cpp
#ifdef TDESKTOP_EMPLOYEE_MODE
// UiDisableMentionTooltip has reversed semantics:
// has(key) == true means the user WANTS mention tooltips disabled.
if (parsed.query.startsWith('@')
	&& session->account().employeePermissions().has(
		Intro::Employee::PermissionKey::UiDisableMentionTooltip)) {
	parsed.query.clear();
}
#endif
```

The exact `session` accessor here is whatever `InitFieldAutocomplete` already captures. Grep the function's parameter list to find it (likely `not_null<Main::Session*> session` or similar).

### Step 3: Add gated include to each modified file

- [ ] 5 files. Each gets the gated include block.

### Step 4: Chinese bullet summary

- [ ] Emit:

```
编译前总结（Task 6：folder.edit + folder.addChat + ui.disableMentionTooltip）：
- settings_folders.cpp：Create/edit/remove 三处 GuardAction(FolderEdit)
- chat_filters_tabs_strip.cpp：tab 右键 Edit/Remove 两处 GuardAction
- window_filters_menu.cpp：sidebar Edit/Remove 两处 GuardAction
- choose_filter_box.cpp：FillChooseFilterMenu 生成的每个 QAction 过 GuardAction；validator.add 回调顶部 Allowed 早退
- field_autocomplete.cpp：InitFieldAutocomplete 的 check lambda 里，@ 分支前用 .has(UiDisableMentionTooltip) 直接清空 query（反向语义，加中文注释）
- 5 个文件 gated include
请编译验证。
```

### Step 5: Smoke test directive

- [ ] Ask user:

1. 撤销 `FolderEdit` → 设置→文件夹中 Create / Edit / Remove 全灰；顶部 Tab 右键 Edit / Remove 灰；左侧 sidebar 右键 Edit / Remove 灰。
2. 撤销 `FolderAddChat` → 会话列表右键"加入文件夹"子菜单的每个文件夹都灰。
3. 开启 `UiDisableMentionTooltip` → 在群聊输入 `@` 不出补全气泡；输入 `/` 命令和 `:` 表情仍正常出气泡。
4. 关闭该权限 → `@` 补全恢复。

### Step 6: Commit

- [ ] After smoke:

```bash
git add Telegram/SourceFiles/settings/sections/settings_folders.cpp \
        Telegram/SourceFiles/ui/widgets/chat_filters_tabs_strip.cpp \
        Telegram/SourceFiles/window/window_filters_menu.cpp \
        Telegram/SourceFiles/boxes/choose_filter_box.cpp \
        Telegram/SourceFiles/chat_helpers/field_autocomplete.cpp
git commit -m "$(cat <<'EOF'
feat(employee): disable folder.* + mention tooltip

FolderEdit gated on create/edit/remove across settings, tab strip,
and sidebar. FolderAddChat gated inside the shared FillChooseFilterMenu
funnel plus validator callback. UiDisableMentionTooltip suppresses
the @-completion popup in field_autocomplete (reversed semantic).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: msg.delete

11 sites across 9 files. Large because delete has entries in message menus, key handlers, list widgets, info pages, and peer menus.

**Files:**
- Modify: `Telegram/SourceFiles/history/view/history_view_context_menu.cpp`
- Modify: `Telegram/SourceFiles/ui/controls/delete_message_context_action.cpp`
- Modify: `Telegram/SourceFiles/history/view/history_view_list_widget.cpp`
- Modify: `Telegram/SourceFiles/history/view/history_view_chat_section.cpp`
- Modify: `Telegram/SourceFiles/history/view/history_view_pinned_section.cpp`
- Modify: `Telegram/SourceFiles/history/view/history_view_scheduled_section.cpp`
- Modify: `Telegram/SourceFiles/info/info_top_bar.cpp`
- Modify: `Telegram/SourceFiles/info/media/info_media_list_widget.cpp`
- Modify: `Telegram/SourceFiles/window/window_peer_menu.cpp`

### Step 1: Context analysis

- [ ] Grep anchors:

```bash
grep -n "tr::lng_context_delete_msg\|tr::lng_context_delete_selected\|listDeleteRequest\|keyPressEvent.*Delete\|Qt::Key_Delete" \
     Telegram/SourceFiles/history/view/history_view_context_menu.cpp \
     Telegram/SourceFiles/history/view/history_view_list_widget.cpp

grep -n "listDeleteRequest" \
     Telegram/SourceFiles/history/view/history_view_chat_section.cpp \
     Telegram/SourceFiles/history/view/history_view_pinned_section.cpp \
     Telegram/SourceFiles/history/view/history_view_scheduled_section.cpp

grep -n "DeleteMessageContextAction\|delete_message_context_action" Telegram/SourceFiles/ui/controls/delete_message_context_action.cpp

grep -n "tr::lng_selected_delete\|deleteRequests\|delete\b" \
     Telegram/SourceFiles/info/info_top_bar.cpp \
     Telegram/SourceFiles/info/media/info_media_list_widget.cpp | head -15

grep -n "addClearHistory\|addDeleteChat" Telegram/SourceFiles/window/window_peer_menu.cpp
```

### Step 2: Site insertions

| # | File | Anchor | Guard | Permission |
|---|------|--------|-------|------------|
| 7.1 | `history_view_context_menu.cpp` | `tr::lng_context_delete_msg` addAction | GuardAction | `MsgDelete` |
| 7.2 | `ui/controls/delete_message_context_action.cpp` | the widget's QAction-owning constructor or the `setEnabled` hook point (grep for `setEnabled\|_action`) | GuardAction (if a QAction is exposed) or BindDisabled (if the widget itself is the button) | `MsgDelete` |
| 7.3 | `history_view_list_widget.cpp` | the Del / Backspace key handler (grep for `Qt::Key_Delete` in `keyPressEvent`) | Allowed early return | `MsgDelete` |
| 7.4 | `history_view_chat_section.cpp` | `listDeleteRequest` override body (first line) | Allowed early return | `MsgDelete` |
| 7.5 | `history_view_pinned_section.cpp` | `listDeleteRequest` body | Allowed early return | `MsgDelete` |
| 7.6 | `history_view_scheduled_section.cpp` | `listDeleteRequest` body | Allowed early return | `MsgDelete` |
| 7.7 | `history_view_context_menu.cpp` | `tr::lng_context_delete_selected` addAction | GuardAction | `MsgDelete` |
| 7.8 | `info/info_top_bar.cpp` | the Info top-bar "Delete Selected" button | BindDisabled | `MsgDelete` |
| 7.9 | `info/media/info_media_list_widget.cpp` | the batch-delete action button | BindDisabled | `MsgDelete` |
| 7.10 | `window_peer_menu.cpp` | `Filler::addClearHistory` (the `addAction(tr::lng_profile_clear_history)`) | GuardAction | `MsgDelete` |
| 7.11 | `window_peer_menu.cpp` | `Filler::addDeleteChat` — **note**: this was also guarded under `GroupDelete` in Task 4. For `MsgDelete`, keep `GroupDelete` guard (it's already there) and ALSO check if `MsgDelete` should apply. Per spec §5.3, `msg.delete` also covers "delete chat", so add a combined check via `rpl::combine` OR simply add an additional `GuardAction(MsgDelete)` which AND-combines because `QAction::setEnabled` called twice with false takes the last one. Simpler: call both sequentially; final state is the latest call. | Skip here (already in Task 4 under `GroupDelete`) | — |

Decision for 7.11: **skip** — Task 4 already gates `addDeleteChat` with `GroupDelete`. Per spec §5.3 the audit listed `addDeleteChat` under msg.delete too, but operationally a delete-chat action semantically maps more cleanly to "group.delete" in our enum (it tears the chat down, which includes removing the user from a group or destroying a one-on-one). No double gate here.

### Step 3: Add gated include to each modified file

- [ ] 8 files total (skip any already-included from earlier tasks: `window_peer_menu.cpp` already has it from Task 4).

### Step 4: Chinese bullet summary

- [ ] Emit:

```
编译前总结（Task 7：msg.delete）：
- history_view_context_menu.cpp：右键"删除" / "删除选中"两处 GuardAction
- ui/controls/delete_message_context_action.cpp：删除菜单项控件本身 GuardAction 或 BindDisabled（视实际结构）
- history_view_list_widget.cpp：Del/Backspace 键处理顶部 Allowed 早退
- history_view_chat_section.cpp / pinned_section.cpp / scheduled_section.cpp：listDeleteRequest 顶部 Allowed 早退
- info_top_bar.cpp + info_media_list_widget.cpp：批量删除按钮 BindDisabled
- window_peer_menu.cpp：addClearHistory GuardAction
- 8 个文件 gated include（window_peer_menu.cpp 已有）
请编译验证。
```

### Step 5: Smoke test directive

- [ ] Ask user:

1. 撤销 `MsgDelete` → 右键任一己发消息"删除"灰；Del / Backspace 无反应；顶栏"清空聊天"灰；info 页批量删除按钮灰。
2. 测试 pinned section、scheduled section 的删除键。
3. 全开 → 所有入口恢复。

### Step 6: Commit

- [ ] After smoke:

```bash
git add Telegram/SourceFiles/history/view/history_view_context_menu.cpp \
        Telegram/SourceFiles/ui/controls/delete_message_context_action.cpp \
        Telegram/SourceFiles/history/view/history_view_list_widget.cpp \
        Telegram/SourceFiles/history/view/history_view_chat_section.cpp \
        Telegram/SourceFiles/history/view/history_view_pinned_section.cpp \
        Telegram/SourceFiles/history/view/history_view_scheduled_section.cpp \
        Telegram/SourceFiles/info/info_top_bar.cpp \
        Telegram/SourceFiles/info/media/info_media_list_widget.cpp \
        Telegram/SourceFiles/window/window_peer_menu.cpp
git commit -m "$(cat <<'EOF'
feat(employee): disable msg.delete entry points

Right-click Delete / Delete Selected, Del/Backspace key, per-section
listDeleteRequest, info batch-delete buttons, and Clear History peer
menu gated via GuardAction / BindDisabled / Allowed early return.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: msg.send (non-SendButton)

~13 sites across 6 files. All msg.send entries except the SendButton visual itself (Task 9).

**Files:**
- Modify: `Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp`
- Modify: `Telegram/SourceFiles/menu/menu_send.cpp`
- Modify: `Telegram/SourceFiles/core/shortcuts.cpp`
- Modify: `Telegram/SourceFiles/history/view/controls/history_view_voice_record_bar.cpp`
- Modify: `Telegram/SourceFiles/boxes/send_files_box.cpp`
- Modify: `Telegram/SourceFiles/history/history_drag_area.cpp`
- Modify: `Telegram/SourceFiles/history/view/history_view_chat_section.cpp`

### Step 1: Context analysis

- [ ] Grep all anchors:

```bash
grep -n "tr::lng_silent_send\|tr::lng_scheduled_send\|tr::lng_send_silent_message\|ScheduleBox" \
     Telegram/SourceFiles/menu/menu_send.cpp

grep -n "JustSendMessage\|SendSilentMessage\|ScheduleMessage\|Shortcut" \
     Telegram/SourceFiles/core/shortcuts.cpp | head -20

grep -n "_sendVoiceRequests\|voiceSend\|sendVoice" \
     Telegram/SourceFiles/history/view/controls/history_view_voice_record_bar.cpp | head -15

grep -n "sendRequests\|submitRequests\|Enter\|KeyEnter" \
     Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp | head -20

grep -n "fileChosen\|inlineResultChosen\|photoChosen\|documentChosen" \
     Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp | head -15

grep -n "Fire\|send\|confirm" Telegram/SourceFiles/boxes/send_files_box.cpp | grep -i "send" | head -10
grep -n "drop\|dropEvent\|sendFiles" Telegram/SourceFiles/history/history_drag_area.cpp | head -10
grep -n "confirmForwardSelected\|confirmSendingFiles\|sendBotCommand" Telegram/SourceFiles/history/view/history_view_chat_section.cpp | head -10
```

### Step 2: Site insertions

| # | File | Anchor | Guard | Permission |
|---|------|--------|-------|------------|
| 8.1 | `history_view_compose_controls.cpp` | the Enter-key / submit handler (top of the lambda fired by `_send` click OR by Enter) | Allowed early return | `MsgSend` |
| 8.2 | `menu/menu_send.cpp` | the 3 "Send Silent / Schedule / Without Sound" menu items (grep for `tr::lng_silent_send` etc.) | GuardAction × 3 | `MsgSend` |
| 8.3 | `core/shortcuts.cpp` | `JustSendMessage` / `SendSilentMessage` / `ScheduleMessage` handler bodies | Allowed early return × 3 | `MsgSend` |
| 8.4 | `history_view_voice_record_bar.cpp` | top of `_sendVoiceRequests.fire(...)` firing path (the lambda executed on record-complete or send-hold-release); also the record-start button itself | Allowed early return on send path; BindDisabled on the record button widget | `MsgSend` |
| 8.5 | `history_view_compose_controls.cpp` | sticker chosen handler (`fileChosen` / `stickerChosen`) | Allowed early return | `MsgSend` |
| 8.6 | `history_view_compose_controls.cpp` | inline / GIF chosen handler (`inlineResultChosen`) | Allowed early return | `MsgSend` |
| 8.7 | `history_view_compose_controls.cpp` | photo panel chosen handler | Allowed early return | `MsgSend` |
| 8.8 | `boxes/send_files_box.cpp` | the Send button inside SendFilesBox | GuardAction (if returned QAction) or BindDisabled (if `Ui::RoundButton`) | `MsgSend` |
| 8.9 | `history/history_drag_area.cpp` | `dropEvent` handler entry (before calling `sendFilesWithConfirmation` or equivalent) | Allowed early return | `MsgSend` |
| 8.10 | `history_view_chat_section.cpp` | `confirmForwardSelected` / forward-flow send commit | Allowed early return | `MsgSend` |
| 8.11 | `history_view_chat_section.cpp` | `confirmSendingFiles` attachment confirm | Allowed early return | `MsgSend` |
| 8.12 | `history_view_chat_section.cpp` | `sendBotCommand` | Allowed early return | `MsgSend` |

### Step 3: Add gated include to each modified file

- [ ] 7 files (`history_view_compose_controls.cpp` is reused from Task 3 — include already present).

### Step 4: Chinese bullet summary

- [ ] Emit:

```
编译前总结（Task 8：msg.send，不含 SendButton）：
- compose_controls.cpp：Enter 提交、sticker / GIF / photo chosen 四处 Allowed 早退
- menu_send.cpp：静默 / 定时 / 静默(无声) 三个菜单项 GuardAction
- shortcuts.cpp：JustSendMessage / SendSilentMessage / ScheduleMessage 三处 Allowed 早退
- voice_record_bar.cpp：语音发送路径 Allowed 早退；录音按钮 BindDisabled
- send_files_box.cpp：Send 按钮 GuardAction/BindDisabled
- history_drag_area.cpp：dropEvent 顶部 Allowed 早退
- history_view_chat_section.cpp：confirmForwardSelected / confirmSendingFiles / sendBotCommand 三处 Allowed 早退
- 6 个文件 gated include（compose_controls.cpp 已有）
注意：SendButton 本身的视觉置灰放 Task 9 处理
请编译验证。
```

### Step 5: Smoke test directive

- [ ] Ask user:

1. 撤销 `MsgSend` → Enter 发不出；菜单发送（静默/定时）项灰；快捷键无反应；录音按钮灰（按不下）；拖拽文件放到聊天区无反应；附件 Box 的 Send 按钮灰；@bot 命令无反应。
2. 全开 → 一切正常。
3. 回归：输入框仍能输入文字 / 打字不受影响 / 草稿自动保存。

### Step 6: Commit

- [ ] After smoke:

```bash
git add Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp \
        Telegram/SourceFiles/menu/menu_send.cpp \
        Telegram/SourceFiles/core/shortcuts.cpp \
        Telegram/SourceFiles/history/view/controls/history_view_voice_record_bar.cpp \
        Telegram/SourceFiles/boxes/send_files_box.cpp \
        Telegram/SourceFiles/history/history_drag_area.cpp \
        Telegram/SourceFiles/history/view/history_view_chat_section.cpp
git commit -m "$(cat <<'EOF'
feat(employee): disable msg.send non-SendButton entry points

Enter / shortcut / voice / sticker / inline / photo / drag-drop /
SendFilesBox / forward / attachment / bot command entries gated via
Allowed early return or GuardAction. SendButton itself handled next.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: msg.send — SendButton combine (special)

1 file, 1 site, but complex. `Ui::SendButton` has an internal state machine that naive `setDisabled` would conflict with. We extend the existing "can send by chat restriction" rpl pipeline in `ComposeControls` with our permission.

**Files:**
- Modify: `Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp`

### Step 1: Context analysis

- [ ] Read the ComposeControls constructor / init section to find how `_send` is wired:

```bash
grep -n "_send\|SendButton\|writeRestriction\|canSend\|rpl::combine" \
     Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp | head -40
```

- [ ] Locate the `rpl::combine` or state-producer that currently feeds the SendButton its enabled/visible state (typically tied to `HistoryView::WriteRestriction` or `Data::CanSendAnything`).

- [ ] Document what you find: the current signal might be a `rpl::producer<bool>` member named something like `_canSendMessages` or a computed producer inside a setup function. You need to AND it with the employee permission.

### Step 2: Extend the combine

- [ ] There are two common patterns in tdesktop; the exact one depends on how `ComposeControls` currently builds the SendButton state. Pick whichever matches:

**Pattern A**: an existing `rpl::producer<bool>` feeding a lambda that calls `_send->setState(...)`. Wrap it with `rpl::combine`:

```cpp
// BEFORE (schematic):
canSendMessagesProducer
    | rpl::start_with_next([=](bool canSend) {
        _send->setState(computeState(canSend, ...));
    }, lifetime());

// AFTER:
auto canSendByEmployee =
#ifdef TDESKTOP_EMPLOYEE_MODE
    session->account().employeePermissions().value(
        Intro::Employee::PermissionKey::MsgSend);
#else
    rpl::single(true);
#endif

rpl::combine(
    canSendMessagesProducer,
    std::move(canSendByEmployee)
) | rpl::map([](bool byChat, bool byEmployee) {
    return byChat && byEmployee;
}) | rpl::start_with_next([=](bool canSend) {
    _send->setState(computeState(canSend, ...));
}, lifetime());
```

**Pattern B**: a boolean member that is recomputed on various events. Wherever it's assigned, OR the assignment with `permissions.has(MsgSend)`:

```cpp
// Locate the assignments and wrap them. Also subscribe to permission
// changes to re-run the same recompute when permissions flip:

#ifdef TDESKTOP_EMPLOYEE_MODE
session->account().employeePermissions().value(
    Intro::Employee::PermissionKey::MsgSend
) | rpl::start_with_next([this](bool) {
    recomputeSendButtonState();  // the existing private helper
}, lifetime());
#endif
```

### Step 3: Alternative fallback — if §4.13 pattern proves too invasive

If tracing the existing SendButton state pipeline in ComposeControls would require more than ~20 lines of changes, fall back to the minimal approach: `BindDisabled(&session, PermissionKey::MsgSend, _send)` at the end of ComposeControls setup. This will call `setDisabled(true)` which overrides the internal state machine's visible state. Acceptable degradation because:
- The send button is not animatable via disabled (the state machine would just not advance)
- No send path is reachable anyway (Task 8 closes Enter/shortcut/etc.)

Document the choice (§2 or §3) in the commit message.

### Step 4: Chinese bullet summary

- [ ] Emit:

```
编译前总结（Task 9：SendButton 特殊处理）：
- compose_controls.cpp：扩展 SendButton 现有 canSend rpl 管道，AND 上 employee MsgSend 权限
  （若现有管道重构成本高，退化为对 _send 直接 BindDisabled）
- 选择方案： [A/B/退化]（由 Step 1-2 的侦察决定）
- 未改其他文件
请编译验证。
```

### Step 5: Smoke test directive

- [ ] Ask user:

1. 撤销 `MsgSend` → SendButton 按钮视觉上置灰（或内部状态机反映 disabled）；点击无反应；Enter 也不触发（由 Task 8 覆盖）。
2. 进入语音录制状态时撤销权限 → 录音结束后发送按钮仍灰（Task 8 的 voice 路径也早退）。
3. 恢复权限 → SendButton 恢复正常状态（有文字亮、无文字 dim 等原有语义）。

### Step 6: Commit

- [ ] After smoke:

```bash
git add Telegram/SourceFiles/history/view/controls/history_view_compose_controls.cpp
git commit -m "$(cat <<'EOF'
feat(employee): disable SendButton visual via MsgSend permission

Extends the existing ComposeControls canSend rpl pipeline with an
AND of the employee MsgSend permission so the send button visually
reflects the permission without fighting its internal state machine.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Final smoke pass + tag

Full regression across all 14 permissions + refresh behavior. No code changes unless bugs surface.

### Step 1: Full smoke matrix (from spec §7.1)

- [ ] Ask user to run through each row of this matrix, flipping the named permission server-side and waiting ≤ 60 s:

| Permission | Verification |
|---|---|
| msg.send | 输入框可打字；Enter / 拖拽 / 贴纸 / GIF / 照片 / SendFilesBox / 录音 / bot 命令 / 快捷键 / 菜单发送(静默/定时) 全部失效；SendButton 灰 |
| msg.edit | 右键"编辑"灰；↑ 键无反应 |
| msg.delete | 右键"删除"/"删除选中"灰；Del/Backspace 无反应；pinned/scheduled section 删除无反应；info 批量删除灰；"清空聊天"灰 |
| msg.forward | 右键"转发"/"转发选中"灰 |
| group.create | 主菜单"新建群组/频道"灰；showNewGroup/showNewChannel 兜底早退 |
| group.delete | 会话右键"删除/退出"灰；info "退出频道"灰 |
| group.addMember | 成员页加号灰；info "添加成员"灰；右键"添加成员"灰 |
| group.removeMember | 成员管理右键 Kick/Restrict/Edit admin 灰 |
| contact.add | 全部 7 处入口灰 |
| contact.block | 全部 7 处入口灰 |
| contact.editNote | info + 菜单两处"编辑联系人"灰 |
| folder.edit | 设置 + tab strip + sidebar 所有 edit/remove/create 灰 |
| folder.addChat | "加入文件夹"子菜单所有项灰 |
| ui.disableMentionTooltip | 开启 → `@` 补全气泡不出；关闭 → 正常出 |

### Step 2: Refresh behavior smoke

- [ ] Ask user:

1. 全权限登录 → 观察约 70 s 内后端收到 `/api/auth/verify` 请求（首轮 jitter 后）。
2. 保持客户端运行 5 分钟 → 后端观察到 5 次请求（~每 60 s 一次）。
3. 停后端 → 客户端日志显式 `failed kind=* count=1`；等 10 分钟再看 `count=10`；中间无日志。
4. 恢复后端 → 下次 tick 出 `recovered after N failures`。
5. 后端将 token 设为失效 → 60 s 内客户端硬登出回到 Intro。
6. 测试后台 / 失焦场景：最小化窗口 2 分钟再打开 → 期间 tick 照常发生（日志可验证）。

### Step 3: Regression smoke

- [ ] Ask user:

1. 完全关闭 TDESKTOP_EMPLOYEE_MODE 重编译一次（若可行）→ 验证普通 tdesktop 用户行为完全未变（右键菜单项全亮，send 按钮正常状态机）。
2. 若无法禁用 flag：开启 mode + 全权限，确认用户体验与普通 tdesktop 无差别。

### Step 4: Tag and push

- [ ] After all smoke passes, create the annotated tag and push:

```bash
git tag -a employee-permissions-v2 -m "Phase 2: UI disable layer + 60s periodic refresh"
git push origin customization
git push origin employee-permissions-v2
```

Do NOT execute without explicit user confirmation (per global "risky action" rules).

---

## Spec Coverage Self-Check

| Spec requirement | Covered by |
|---|---|
| §2 Helper layer (GuardAction / BindDisabled / Allowed) | Task 1 |
| §2 `#ifdef` gating everywhere | Tasks 1-9 (G1 applies universally) |
| §2 Session access via `session->account().employeePermissions()` | Task 1 cpp + all UI tasks use this path |
| §2 No new Session pass-through member | Confirmed: UI tasks use `session->account()` directly |
| §2.3-2.4 `_employeeVerifyTimer` + 3 methods + wiring | Task 2 |
| §3.2 CMake registration | Task 1 Step 4 |
| §4 Data flow (widget construct / timer tick) | Tasks 1-2 implement; Tasks 3-9 consume |
| §4.5 Timer lifecycle across cold-start/bootstrap/reset | Task 2 Steps 4-6 |
| §4.6 Log throttling | Task 2 Step 7 (onEmployeeVerifyTimerTick body) |
| §5.1-5.14 all 14 permissions' UI sites | Tasks 3-9 (table per task) |
| §5.13 SendButton special handling | Task 9 |
| §5.12 ui.disableMentionTooltip inverted semantic note | Task 6 site 6.10 |
| §7 Smoke testing | Every UI task's Step 5; Task 10 aggregates |
| §8 Risk: click-race, SendButton conflict, log throttling | Task 9 Step 3 (fallback), Task 2 log throttling |
| §11 Implementation ordering | Tasks 1→2→3→...→10 matches spec's suggested order |
