# Employee Permissions — Phase 2 (UI Disable + Periodic Refresh) Design Spec

**Date:** 2026-04-23
**Status:** Approved
**Depends on:** Phase 1 (tag `employee-permissions-v1` @ 63ccfe7)
**Scope explicitly excluded from this phase:**
- API-layer interception (deferred to Phase 3)
- `hiddenFolderNames` feature
- Message activity / read-receipt controls beyond the 14 defined permissions

---

## 1. Context & Goal

Phase 1 landed the data layer:
- 14 `PermissionKey` enum values + mapping table
- `Intro::Employee::Permissions` class backed by `rpl::variable<PermissionValues>`
- `VerifyClient` for one-shot `GET /api/auth/verify`
- Encrypted persistence via `Storage::Account` (file key `lskEmployeeAuth`)
- Cold-start verify in `Main::Account::start()`, 401 → `forcedLogOut()`

Phase 2 closes the user-visible loop: **disable UI entry points** when a permission is `false`, and **refresh permissions every minute** so server-side revocations take effect within ~60 s.

**Success criteria:**
1. For every one of ~60 UI entry points cataloged in the audit, the action cannot be triggered when the corresponding permission is `false`.
2. Disabled UI is visually distinguishable (greyed out via `setDisabled(true)` + `WA_TransparentForMouseEvents`), not hidden.
3. Permissions refresh automatically every 60 s (with 0-10 s first-tick jitter).
4. Refresh failures other than 401 preserve the current permission snapshot.
5. 401 during refresh triggers hard logout (same path as Phase 1 cold-start).
6. No existing tdesktop UI behavior regresses when `TDESKTOP_EMPLOYEE_MODE` is off.

**Non-goals:**
- No API-layer safety net. UI-only means a race (click arriving in the 1-frame window where permission just flipped to `false`) can still fire one request; Phase 3 will cover that.
- No toast or popup when user clicks a disabled control. Disabled appearance is the only feedback.

---

## 2. Architecture

Two new additions, zero modifications to Phase 1 semantics:

```
┌────────────────────────────────────────────────────────────┐
│ Phase 1 (unchanged)                                         │
│  Permissions   VerifyClient   AuthSnapshot   Storage        │
│  Main::Account::kickOffEmployeeVerifyIfAuthorized           │
│  Main::Account::handleEmployeeAuthMissingOrCorrupt          │
└────────────────────────────────────────────────────────────┘
         ▲                              ▲
         │ read permissions             │ apply permission updates
         │                              │
┌────────┴────────┐       ┌─────────────┴──────────────┐
│ Phase 2 NEW:    │       │ Phase 2 NEW:                │
│  ui_guard.{h,cpp}│       │ Main::Account::             │
│                 │       │   _employeeVerifyTimer       │
│ GuardAction     │       │   (base::Timer, 60 s)        │
│ BindDisabled    │       │                              │
│ Allowed         │       │ tick → VerifyClient          │
│                 │       │  success → apply + persist   │
│ called 1 line   │       │  401     → forcedLogOut      │
│ per UI site     │       │  other   → log + keep        │
│ (~60 sites)     │       │                              │
└─────────────────┘       └──────────────────────────────┘
```

**Design principles:**
- Every Phase 2 file is gated by `#ifdef TDESKTOP_EMPLOYEE_MODE`.
- No new Session member; UI code reaches permissions via `session->account().employeePermissions()`.
- No abstraction over the existing `Permissions` / `VerifyClient` — we add small adapters, not a new layer.
- Each UI site integration is **one line** plus an include.

---

## 3. Components

### 3.1 New: `intro/employee/employee_ui_guard.h`

```cpp
#pragma once
#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/required.h"
#include "intro/employee/employee_permissions.h"

class QAction;
namespace Main { class Session; }
namespace Ui { class RpWidget; }

namespace Intro::Employee {

// One-shot: read current permission and setEnabled(QAction) accordingly.
// For short-lived widgets (QMenu items, transient Box buttons) that are
// rebuilt on each user interaction.
void GuardAction(
    not_null<Main::Session*> session,
    PermissionKey key,
    not_null<QAction*> action);

// Reactive: subscribe to permission changes and propagate to widget.
// When permission is false: widget->setDisabled(true) + WA_TransparentForMouseEvents=true.
// Subscription attached to widget->lifetime(); auto-released on destruction.
// For long-lived widgets (compose controls, info page action buttons, sidebars).
void BindDisabled(
    not_null<Main::Session*> session,
    PermissionKey key,
    not_null<Ui::RpWidget*> widget);

// Pure read. Used in callback early-return for non-button inputs:
// Enter key, Delete key, drag-drop, paste, keyboard shortcuts, bot commands.
[[nodiscard]] bool Allowed(
    not_null<Main::Session*> session,
    PermissionKey key);

} // namespace Intro::Employee

#endif // TDESKTOP_EMPLOYEE_MODE
```

### 3.2 New: `intro/employee/employee_ui_guard.cpp`

Implementation sketch (final form emerges in the plan):

```cpp
void GuardAction(session, key, action) {
    action->setEnabled(session->account().employeePermissions().has(key));
}

void BindDisabled(session, key, widget) {
    session->account().employeePermissions().value(key)
        | rpl::start_with_next([w = widget.get()](bool allowed) {
            w->setDisabled(!allowed);
            w->setAttribute(Qt::WA_TransparentForMouseEvents, !allowed);
        }, widget->lifetime());
}

bool Allowed(session, key) {
    return session->account().employeePermissions().has(key);
}
```

`Permissions::value()` already emits the current value immediately on subscribe and passes through `rpl::distinct_until_changed`, so no extra plumbing needed.

### 3.3 Modified: `Main::Account`

**New member (in `#ifdef` block, next to `_employeeVerify`):**
```cpp
base::Timer _employeeVerifyTimer;
int _employeeVerifyConsecutiveFailures = 0;
```

**New private methods:**
- `startEmployeeVerifyTimer()` — first tick at `60s + rand(0, 10s)`; subsequent ticks `60s` flat
- `stopEmployeeVerifyTimer()` — `cancel()` + reset failure counter
- `onEmployeeVerifyTimerTick()` — timer callback; calls `_employeeVerify->verify(...)` with result handler

**Existing methods extended (additive only):**
- `kickOffEmployeeVerifyIfAuthorized()` — on cold-start success, additionally `startEmployeeVerifyTimer()`
- `applyEmployeeBootstrap()` — after existing body, `startEmployeeVerifyTimer()`
- `applyEmployeeReset()` — at top, `stopEmployeeVerifyTimer()`

### 3.4 UI file changes (~20 files, ~60 sites)

Each file gets `#include "intro/employee/employee_ui_guard.h"` (gated) and 1-3 added lines (also gated). Full site-by-site mapping in §5.

### 3.5 CMake

`Telegram/CMakeLists.txt` registers the two new files:
```
intro/employee/employee_ui_guard.cpp
intro/employee/employee_ui_guard.h
```
Alphabetical ordering within the existing `intro/employee/` block.

---

## 4. Data Flow

### 4.1 Widget construction (static: `GuardAction`)

```
menu->addAction(tr::lng_context_delete_msg(...), callback);
GuardAction(session, PermissionKey::MsgDelete, action);
    → session->account().employeePermissions().has(MsgDelete)
    → action->setEnabled(permitted)
```
User sees the correct greyed/solid state the instant the menu pops up. Menu is discarded on close; next open re-runs GuardAction with the latest snapshot.

### 4.2 Widget construction (reactive: `BindDisabled`)

```
BindDisabled(session, PermissionKey::MsgSend, _send);
    → permissions.value(MsgSend)                   // emits current value now
      | rpl::start_with_next([w](bool ok) {
            w->setDisabled(!ok);
            w->setAttribute(Qt::WA_TransparentForMouseEvents, !ok);
        }, w->lifetime());
```
When the widget is destroyed, `lifetime()` releases the subscription.

### 4.3 Refresh tick — success path

```
cold-start verify success (or bootstrap)
  → Account::startEmployeeVerifyTimer()
      first delay = 60s + rand(0, 10s)
  → base::Timer fires → onEmployeeVerifyTimerTick()
  → _employeeVerify->verify(backend, token, crl::guard(this, lambda))
  → VerifySuccess{newValues}
      permissions.apply(newValues, oldToken)        // token unchanged
      local().writeEmployeeAuth(Serialize(...))     // persist fresh snapshot
      _employeeVerifyConsecutiveFailures = 0
      timer.callOnce(60_s)                          // next tick, no jitter
  → rpl::variable emits → all BindDisabled subscribers update instantly
  → next menu open → GuardAction reads the new value
```

### 4.4 Refresh tick — failure paths

```
VerifyFailure:
  Network / Server(4xx/5xx) / BadJson:
      ++consecutiveFailures
      if (n == 1 || n % 10 == 0) LOG("Employee: verify tick failed kind=K n=N")
      timer.callOnce(60_s)                           // keep trying; UI unchanged

  InvalidToken (401):
      stopEmployeeVerifyTimer()
      handleEmployeeAuthMissingOrCorrupt()           // Phase 1 logic:
                                                     //   crl::on_main → forcedLogOut
```

### 4.5 Timer lifecycle

| Event | Action |
|---|---|
| Cold start + token exists + verify ok | start timer (first = 60s + jitter) |
| Cold start + 401 | timer never starts; Phase 1 hard logout |
| Cold start + Network/5xx | start timer (keep retrying every 60s) |
| Login success via `applyEmployeeBootstrap` | start timer |
| Hard logout / `applyEmployeeReset` | stop timer, reset failure counter |
| Account destruction | timer released with the unique_ptr/member |

### 4.6 Log throttling

To avoid 1 log/minute under sustained backend outage:
- Log on failure count 1, 10, 20, 30, ... (`n == 1 || n % 10 == 0`)
- Log once on recovery: `"Employee: verify recovered after N failures"`

---

## 5. UI Site Map (Implementation Blueprint)

Audit done in preceding session; total ~60 sites across ~20 files. Each row gets exactly one Guard call.

### 5.1 msg.send (14 sites + 1 auxiliary)

| Site | Guard |
|---|---|
| `history_view_compose_controls.cpp:1772` send button | **Special** (see §5.13) |
| `history_view_compose_controls.cpp:1773` Enter submit | Allowed early return |
| `menu_send.cpp:874-887` send menu (silent / schedule, 3 items) | GuardAction × 3 |
| `core/shortcuts.cpp:132,155` JustSendMessage | Allowed early return |
| `core/shortcuts.cpp:133,156` SendSilentMessage | Allowed early return |
| `core/shortcuts.cpp:134` ScheduleMessage | Allowed early return |
| `history_view_voice_record_bar.cpp:3008,3038,3109` voice send | Allowed early return |
| `history_view_compose_controls.cpp:2461` sticker chosen | Allowed early return |
| `history_view_compose_controls.cpp:1890` inline/GIF send | Allowed early return |
| `history_view_compose_controls.cpp:1889` photo panel send | Allowed early return |
| `send_files_box.cpp:948` SendFilesBox send button | GuardAction |
| `history_drag_area.cpp:369` drag-drop | Allowed early return |
| `history_view_chat_section.cpp:3415` forward commit | Allowed early return |
| `history_view_chat_section.cpp:1231` attachment confirm | Allowed early return |
| `history_view_chat_section.cpp:865` bot command | Allowed early return |
| (aux) voice record start button | BindDisabled |

### 5.2 msg.edit (4 sites)

| Site | Guard |
|---|---|
| `history_view_context_menu.cpp:750` right-click Edit | GuardAction |
| `history_widget.cpp:7990` ↑ edit last | Allowed early return |
| `history_view_compose_controls.cpp:2323` ↑ edit last (compose path) | Allowed early return |
| `history_widget.cpp:8001` ↑ edit last media | Allowed early return |

### 5.3 msg.delete (11 sites)

| Site | Guard |
|---|---|
| `history_view_context_menu.cpp:931` right-click Delete | GuardAction |
| `ui/controls/delete_message_context_action.cpp:245` delete context action widget | GuardAction |
| `history_view_list_widget.cpp:2828` Delete/Backspace key | Allowed early return |
| `history_view_chat_section.cpp:2983` listDeleteRequest | Allowed early return |
| `history_view_pinned_section.cpp:614` listDeleteRequest | Allowed early return |
| `history_view_scheduled_section.h:126` listDeleteRequest | Allowed early return |
| `history_view_context_menu.cpp:862` right-click Delete Selected | GuardAction |
| `info/info_top_bar.cpp:736` info batch delete | BindDisabled |
| `info/media/info_media_list_widget.cpp:1278` media batch delete | BindDisabled |
| `window_peer_menu.cpp:799` `addClearHistory` | GuardAction |
| `window_peer_menu.cpp:814` `addDeleteChat` | GuardAction |

### 5.4 msg.forward (3 sites)

| Site | Guard |
|---|---|
| `history_view_context_menu.cpp:429` right-click Forward | GuardAction |
| `history_view_context_menu.cpp:394` right-click Forward Selected | GuardAction |
| forward flow's send step | covered by msg.send §5.1 |

Pre-implementation check: grep ComposeControls for any forward-bar quick-forward entry; if one exists, add a `BindDisabled` site there. If not, no action.

### 5.5 group.create (3 sites)

| Site | Guard |
|---|---|
| `window_main_menu.cpp:685` New Group | GuardAction |
| `window_main_menu.cpp:694` New Channel | GuardAction |
| `WindowSessionController::showNewGroup/showNewChannel` entry | Allowed early return (safety net) |

### 5.6 group.delete (3 sites)

| Site | Guard |
|---|---|
| `window_peer_menu.cpp:804-817` `addDeleteChat` | GuardAction |
| `window_peer_menu.cpp:820-832` `addLeaveChat` | GuardAction |
| `info/profile/info_profile_actions.cpp:2794-2805` Leave Channel | BindDisabled |

### 5.7 group.addMember (3 sites)

| Site | Guard |
|---|---|
| `info_profile_members.cpp:143-145` add-member icon | BindDisabled |
| `info_profile_actions.cpp:2950-2971` Add Member button | BindDisabled |
| `window_peer_menu.cpp:1060-1077` Add Members context menu | GuardAction |

### 5.8 group.removeMember (1 site, 3 actions)

| Site | Guard |
|---|---|
| `edit_participants_box.cpp:1835-1999` participant context menu: Kick / Restrict / Edit Admin | GuardAction × 3 |

### 5.9 contact.add (8 sites)

| Site | Guard |
|---|---|
| `info_profile_actions.cpp:2368` Add to Contacts | BindDisabled |
| `window_peer_menu.cpp:1006` Add to Contacts menu | GuardAction |
| `window_main_menu.cpp:718` New Contact | GuardAction |
| `boxes/peer_list_controllers.cpp:109` Contacts box Add button | BindDisabled |
| `core/phone_click_handler.cpp:342` phone number context | GuardAction |
| `history/view/media/history_view_contact.cpp:97` received-contact card Add | BindDisabled |
| `history/view/history_view_contact_status.cpp:789` chat topbar Add | BindDisabled |
| `dialogs_inner_widget.cpp:3347` dialog list right-click (covered via window_peer_menu; belt-and-braces) | GuardAction |

### 5.10 contact.block (7 sites)

| Site | Guard |
|---|---|
| `info_profile_actions.cpp:2740` Block button | BindDisabled |
| `window_peer_menu.cpp:848` profile menu `addBlockUser` | GuardAction |
| `window_peer_menu.cpp:1750` context menu `addBlockUser` | GuardAction |
| `history_inner_widget.cpp:3111` sender right-click | GuardAction |
| `history_inner_widget.cpp:3382` forward-stamp right-click | GuardAction |
| `history_view_contact_status.cpp:793` topbar Block | BindDisabled |
| `settings/sections/settings_blocked_peers.cpp:131` Blocked list Add | BindDisabled |

### 5.11 contact.editNote (2 sites)

| Site | Guard |
|---|---|
| `info_profile_actions.cpp:2590` Edit Contact button | BindDisabled |
| `window_peer_menu.cpp:1036` Edit Contact menu | GuardAction |

(Share contact is not part of this permission.)

### 5.12 folder.edit, folder.addChat, ui.disableMentionTooltip

**folder.edit (7 sites):**
- `settings_folders.cpp:566` Create filter → GuardAction
- `settings_folders.cpp:496` edit filter row → GuardAction
- `settings_folders.cpp:469` delete filter → GuardAction
- `chat_filters_tabs_strip.cpp:82,95` tab right-click Edit / Remove → GuardAction × 2
- `window_filters_menu.cpp:416,429` sidebar Edit / Remove → GuardAction × 2

**folder.addChat (1 funnel):**
- `boxes/choose_filter_box.cpp:266` `FillChooseFilterMenu` generated items → GuardAction on each generated `QAction`
- `validator.add()` callback (same file around :280-297) → Allowed early return (safety net)

**ui.disableMentionTooltip (1 funnel, inverted semantic):**
- `field_autocomplete.cpp:1754` inside `check` lambda:
  ```cpp
  if (parsed.query.startsWith('@')
      && session->account().employeePermissions()
             .has(PermissionKey::UiDisableMentionTooltip)) {
      parsed.query.clear();  // suppress @ mention popup
  }
  ```
  **Note:** this key's semantic is reversed (`has==true` means *suppress*), so use raw `has()` instead of `Allowed()` to avoid misreading. Add an explanatory comment at the site.

### 5.13 `Ui::SendButton` special handling

`Ui::SendButton` has an internal state machine (`Slowmode` / `Save` / `Schedule` / `Record` / `Cancel`). A naive `setDisabled(true)` conflicts with that state. `ComposeControls` already combines a "can send by chat restriction" signal into the send button's state.

**Approach:** in `ComposeControls` construction, extend the existing combine:
```cpp
rpl::combine(
    existingChatCanSend,
    session->account().employeePermissions().value(PermissionKey::MsgSend))
    | rpl::map([](bool byChat, bool byEmployee) { return byChat && byEmployee; })
    | rpl::start_with_next([...](bool canSend) { /* drive SendButton state */ }, lifetime);
```
Exact combine site and state-feed function discovered during implementation (not `BindDisabled`). Spec-level commitment: **respect SendButton's own disable mechanism; do not force `setDisabled` from outside**.

### 5.14 Counts

| Guard type | Approx sites |
|---|---|
| BindDisabled | ~18 |
| GuardAction | ~28 |
| Allowed early-return | ~15 |
| SendButton special | 1 |
| **Total** | **~60 sites across ~30 files** |

---

## 6. Error Handling

| Scenario | Behavior |
|---|---|
| `session->account().employeePermissions()` accessed before init | Impossible: Phase 1 constructor initialises. `Expects()` as belt-and-braces. |
| `BindDisabled` widget destroyed mid-subscription | `widget->lifetime()` handles release, rpl standard. |
| Timer tick while prior verify still in flight | `VerifyClient::verify()` cancels prior before starting new. Safe. |
| Timer tick when token was just cleared (logout race) | `applyEmployeeReset` stops timer before clearing token. Even if raced, empty-token request → 401 → existing hard-logout, idempotent. |
| 401 in timer callback must defer forcedLogOut | Reuse Phase 1 `handleEmployeeAuthMissingOrCorrupt` (already does `crl::on_main` + `forcedLogOut`). |
| Sustained Network/5xx/BadJson | §4.6 log throttling; UI keeps last-known permissions; timer continues. |
| User has menu open when permission flips | Menu is a snapshot. Next open reflects new state. Acceptable. |
| User's click is in Qt event queue when `BindDisabled` flips to disabled | The dispatched click will still execute its callback. Acknowledged risk, see §8. Phase 3 API-layer interception closes this. |

---

## 7. Testing Strategy

Smoke tests only (Phase 1 precedent). No gtest-level widget tests; those require a full Qt event loop harness not present in this codebase.

### 7.1 Per-permission smoke checks

For each permission, after implementing its sites: log in as a test employee, toggle permission in backend DB, verify within 60 s (or on next relevant UI interaction) that:

| Permission | Verification |
|---|---|
| msg.send | Input field still accepts typing; send button greyed; Enter no-op; drag-drop no-op |
| msg.edit | Right-click Edit greyed; ↑ key no-op |
| msg.delete | Right-click Delete greyed; Del/Backspace no-op; topbar "Clear History" greyed |
| msg.forward | Right-click Forward greyed |
| group.create | Main menu New Group/Channel/Contact items greyed |
| group.delete | Dialog-list right-click "Delete and Leave" greyed; info-page "Leave Channel" greyed |
| group.addMember | Info page "+" greyed; right-click "Add Members" greyed |
| group.removeMember | Member management context menu Kick/Restrict/Edit Admin greyed |
| contact.add | Info-page "Add to Contacts" greyed; main menu "New Contact" greyed |
| contact.block | Info-page Block greyed; sender right-click Block greyed |
| contact.editNote | Info-page Edit Contact greyed; menu Edit Contact greyed |
| folder.edit | Settings → Folders Create/Edit/Delete greyed |
| folder.addChat | Dialog right-click "Add to Folder" submenu all items greyed |
| ui.disableMentionTooltip | Typing `@` in chat input produces no completion popup |

### 7.2 Timer smoke checks

- Launch client; revoke a permission in backend DB; within 60 s observe the corresponding button greying out live (BindDisabled sites).
- Launch client; invalidate token in backend; within 60 s observe hard logout back to Intro screen.
- Stop backend; observe only 2 failure log entries in first 10 ticks (first + 10th).
- Restart backend; next tick produces one recovery log entry.

### 7.3 Regression smoke

- Build with `TDESKTOP_EMPLOYEE_MODE` **off** → verify no behavior change (all guards compiled out).
- Build with mode **on**, all permissions = true → every UI action works as vanilla tdesktop.

---

## 8. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Click-race: user clicks in the 1-frame window where permission just flipped to false | Single action may bypass | Phase 3 API-layer interception closes this; probability is minute-scale rare. |
| Upstream merge conflicts on ~20 UI files | Manual reconciliation per site | Each change is 1 line, `#ifdef`-gated; conflicts are minimal and mechanical. |
| Audit missed an entry point (plugin, new API, future refactor) | That entry bypasses permission | Incremental — field report + patch; not a Phase 2 blocker. |
| `Ui::SendButton` existing restriction logic conflicts with new permission feed | Send button behaves oddly | Read ComposeControls canSend derivation carefully before editing. |
| 60 QPS burst under load (60 employees × 60 s) | Backend pressure | Jitter spreads first tick; backend can monitor and negotiate cadence. |
| Log throttling masks real failures | Diagnostics slower | Throttle preserves first failure + every 10th + recovery — sufficient for trend detection. |
| Employee runs with non-focused window for hours | Permission updates continue per spec | Acceptable; consistent with tdesktop's own `_onlineTimer` / reload timers that run regardless of focus. |

---

## 9. Out of Scope (for reference)

- **Phase 3**: API-layer interception at `ApiWrap`, `Histories`, `ChatParticipants`, `ChatFilters`, `BlockedPeers`, etc. — closes the click-race gap.
- **Phase 4**: Periodic `hiddenFolderNames` fetch + folder hiding in sidebar.
- **Advanced UX**: on-hover tooltip explaining *why* a button is disabled; toast confirmation; admin-only override UI.

---

## 10. File Manifest (for planning)

**New files (2):**
- `Telegram/SourceFiles/intro/employee/employee_ui_guard.h`
- `Telegram/SourceFiles/intro/employee/employee_ui_guard.cpp`

**Modified files — core (2):**
- `Telegram/SourceFiles/main/main_account.h` (add `_employeeVerifyTimer`, `_employeeVerifyConsecutiveFailures`, three private methods)
- `Telegram/SourceFiles/main/main_account.cpp` (timer start/stop wiring; tick callback)

**Modified files — UI sites (~30):**
- `chat_helpers/field_autocomplete.cpp`
- `core/phone_click_handler.cpp`
- `core/shortcuts.cpp`
- `boxes/choose_filter_box.cpp`
- `boxes/peer_list_controllers.cpp`
- `boxes/peers/edit_participants_box.cpp`
- `boxes/send_files_box.cpp`
- `dialogs/dialogs_inner_widget.cpp`
- `history/history_widget.cpp`
- `history/history_inner_widget.cpp`
- `history/history_drag_area.cpp`
- `history/view/history_view_chat_section.cpp`
- `history/view/history_view_compose_controls.cpp`
- `history/view/history_view_contact_status.cpp`
- `history/view/history_view_context_menu.cpp`
- `history/view/history_view_list_widget.cpp`
- `history/view/history_view_pinned_section.cpp`
- `history/view/history_view_scheduled_section.cpp`
- `history/view/history_view_voice_record_bar.cpp`
- `history/view/media/history_view_contact.cpp`
- `info/info_top_bar.cpp`
- `info/media/info_media_list_widget.cpp`
- `info/profile/info_profile_actions.cpp`
- `info/profile/info_profile_members.cpp`
- `menu/menu_send.cpp`
- `settings/sections/settings_blocked_peers.cpp`
- `settings/sections/settings_folders.cpp`
- `ui/controls/delete_message_context_action.cpp`
- `ui/widgets/chat_filters_tabs_strip.cpp`
- `window/window_filters_menu.cpp`
- `window/window_main_menu.cpp`
- `window/window_peer_menu.cpp`
- `window/window_session_controller.cpp`

**Modified build (1):**
- `Telegram/CMakeLists.txt`

---

## 11. Implementation Ordering (for plan)

Suggested task decomposition (final ordering decided in plan):
1. Create `employee_ui_guard.h/.cpp` + CMake + sanity compile
2. Add `_employeeVerifyTimer` + timer lifecycle in `Main::Account` (no UI changes yet)
3. UI integrations grouped by permission family — one task per family:
   - msg.edit, msg.forward (centralised, small)
   - group.removeMember, contact.editNote, folder.addChat, ui.disableMentionTooltip (small)
   - msg.delete (many sites, grouped)
   - msg.send (many sites + SendButton special)
   - contact.add, contact.block (many sites)
   - group.create, group.delete, group.addMember (medium)
   - folder.edit (medium)
4. Final smoke-test pass across all 14 permissions + refresh behavior

Each UI task begins with a **context analysis step**: grep the upstream file for existing patterns (how does this widget currently handle other restriction types?), then insert the guard following that pattern.

---

## 12. Post-Implementation Notes (2026-04-23, shipped as `employee-permissions-v2` @ 8de2f54)

Retrospective annotations from actual execution. The spec above is what was designed; this section captures what actually shipped and why.

### 12.1 Audit gaps discovered mid-execution

The initial UI audit found sites in the `HistoryView` section-path files (`history_view_context_menu.cpp`, `history_view_chat_section.cpp`, `history_view_compose_controls.cpp`, etc.) but systematically missed the **classic** main chat path rooted in `history_widget.cpp` and `history_inner_widget.cpp`. Section-path files only serve secondary views (reply threads, scheduled list, pinned list, topic views). The main chat — what users see 99% of the time — runs through the older `HistoryWidget` / `HistoryInner` classes with their own parallel implementation of context menus and send/delete paths.

Every UI task in Tasks 3, 7, 8 required a second pass to add guards in the main-chat twin files:

| Task | Missing main-chat coverage added |
|---|---|
| Task 3 (msg.edit + msg.forward) | `history_inner_widget.cpp` — 5 sites: Edit + Forward Selected × 2 + Forward Msg × 2 |
| Task 7 (msg.delete) | `history_inner_widget.cpp` — 4 sites: Delete Selected × 2 + single-msg Delete × 2; plus Del/Backspace key handler. `info/info_top_bar.cpp` + `info/media/info_media_list_widget.cpp` batch-delete also added |
| Task 8 (msg.send) | `history_widget.cpp` — 9 sites in the `HistoryWidget::send*` family: `send`, `sendScheduled`, `sendVoice`, `sendBotStartCommand`, `sendBotCommand`, `sendInlineResult`, `sendExistingDocument`, `sendExistingPhoto`, `sendingFilesConfirmed` |

**Lesson for future audits:** when auditing a "user clicks X" path, always check both the `HistoryView::*` (section) and `History*` (classic) implementations; they share surface vocabulary but not code.

### 12.2 Permission-enum extension: Delete Contact → ContactBlock

The original 14-permission enum (Phase 1) does not have a `ContactDelete` key. During Task 9 smoke, the user noticed the "Delete Contact" button in the info sidebar was still clickable when all contact.* permissions were revoked. Resolution: we mapped Delete Contact to `ContactBlock`, reframing the permission's semantic as "destructive action on a contact relationship" — covering both block and delete. Two guards added:
- `window_peer_menu.cpp::Filler::addDeleteContact` — GuardAction(ContactBlock)
- `info/profile/info_profile_actions.cpp::ActionsFiller::addDeleteContactAction` — BindDisabled(ContactBlock)

No enum change. If future requirements need finer-grained control, split into a dedicated `ContactDelete` key in Phase 3+.

### 12.3 rpl API drift: `rpl::on_next` vs `rpl::start_with_next`

This fork's `Telegram/lib_rpl` only exposes `rpl::on_next` (and variants: `on_error`, `on_done`, `on_next_error`, etc.). Upstream tdesktop uses `rpl::start_with_next`; this fork hasn't synced. The original spec drafts used `start_with_next` — a compile error was caught during Task 1's first build attempt. All helpers in `intro/employee/employee_ui_guard.cpp` and subsequent subscriptions use `rpl::on_next`.

### 12.4 Visual nits accepted as known issues

Some long-lived custom-drawn buttons do not visually reflect `setDisabled(true)` because their `paintEvent` implementations don't query the Qt `QPalette::Disabled` role. They remain visually normal but are non-clickable (mouse events transparent via `WA_TransparentForMouseEvents`):

- `Ui::SettingsButton` entries in `window/window_main_menu.cpp` (New Group, New Channel, New Contact)
- Inner-class `Ui::FlatButton` instances in `history/view/history_view_contact_status.cpp` (top-bar Add/Block — widgets are private members of an inner class, so we switched to `Allowed` early-return in the click handler instead of `BindDisabled`)
- `Ui::SendButton` visual fallback: we piggy-back on its built-in `State.forbidden` flag and paint response, but some rendering paths may not repaint in all scenarios. Actual send is blocked via the `Allowed` early-return in Task 8.

The spec's success criterion §2 says "Disabled UI is visually distinguishable." For these specific widgets, the functional outcome is honored (non-clickable) while the visual cue is weaker than a standard grey-out. The user accepted these as known issues; a fix (either a custom-drawn disabled palette overlay or an opacity/color-override technique) is deferred.

### 12.5 Diagnostic logs kept in production

Two diagnostic LOG lines survive in the production code as durable observability:
- `employee_auth.cpp::ParseAuthResponse` — "Employee: login perms parsed: send=%1 edit=%2 ..." dumps the 14 parsed permission bits on every login response
- `employee_permissions.cpp::Permissions::apply` — "Employee: Permissions::apply tokenLen=%1 edit=%2 forward=%3 delete=%4 send=%5" on every apply call (login + every 60s tick)

Plus the Task 2 periodic tick log `Employee: verify tick ok` (every 60s on success), the log-throttled failure count (`Employee: verify tick failed kind=K count=N` at 1, 10, 20, ...), and the recovery line (`Employee: verify recovered after N failures`). These are low-volume and useful for debugging future permission issues.

Temporary in-flight diagnostic LOGs (GuardAction, BindDisabled, Add*Action entry traces, FillContextMenu entry trace) were removed before Task 3 commit once the audit gap was identified.

### 12.6 Final site count (actual shipped)

| Guard type | Shipped count |
|---|---|
| BindDisabled | ~20 |
| GuardAction | ~35 |
| Allowed early-return | ~25 |
| SendButton forbidden-flag integration | 2 sites (HistoryWidget + ComposeControls) |
| **Total** | **~80 sites across ~25 files** |

Slightly higher than the spec's original ~60 estimate, primarily due to the main-chat double-coverage documented in §12.1.

### 12.7 Implementation commits (reference)

| Phase | Commit | Subject |
|---|---|---|
| Helper layer | `3c65a8b` | feat(employee): add UI guard helpers |
| Periodic timer | `2aa870a` | feat(employee): periodic verify timer with jitter + log throttling |
| Diagnostic logs | `d5ecd1f` | diag(employee): log permission payload on login and on apply |
| msg.edit + msg.forward | `15522d3` | feat(employee): disable msg.edit and msg.forward entry points |
| group.* | `2ac84d4` | feat(employee): disable group.* entry points |
| contact.* | `88c3bef` | feat(employee): disable contact.* entry points |
| folder.* + mention tooltip | `d0bd342` | feat(employee): disable folder.* + mention tooltip |
| msg.delete | `db15a2c` | feat(employee): disable msg.delete entry points |
| msg.send (non-SendButton) | `58cb8e4` | feat(employee): disable msg.send non-SendButton entry points |
| SendButton + Delete Contact | `8de2f54` | feat(employee): SendButton forbidden visual + delete-contact gate |
| **Tag** | `employee-permissions-v2` | → `8de2f54` |
