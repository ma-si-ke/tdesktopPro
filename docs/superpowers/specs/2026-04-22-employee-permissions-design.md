# Employee Permissions — Design Spec

- **Date**: 2026-04-22
- **Status**: Approved for implementation planning
- **Depends on**: `2026-04-21-employee-login-redesign.md` (employee login system must be landed first; this spec extends `applyEmployeeBootstrap` and parses additional login response fields).
- **Scope**: Data layer only — storage, load, verify refresh, and API surface for business code. Does **not** include UI disable, business-API interception, periodic polling, or hidden-folders filtering.

---

## 1. Background

The employee mode (`TDESKTOP_EMPLOYEE_MODE`) authenticates against a company backend via `POST /api/auth/login`. The backend response contains four fields used by this client:

- `tdesktopSession` — MTP auth bootstrap (already consumed; see login redesign spec)
- `token` — Bearer token for subsequent server calls (currently discarded)
- `permissions` — object mapping permission key strings to booleans (currently discarded)
- `employee` — employee profile (unused in this spec)

The Web companion client (`client/`) already implements the permission system using the same backend, with 14 known keys and a `GET /api/auth/verify` refresh endpoint. This spec brings equivalent data-layer support to tdesktop so that subsequent phases (UI disable, business API interception) have a stable API to query against.

## 2. Goals

1. Parse and persist `token` + `permissions` from the login response.
2. Expose a typed query API to business code: `const Employee::Permissions & Account::employeePermissions()`.
3. Support reactive subscription to permission changes via `rpl::producer`.
4. On cold start with a persisted account, read the local copy **first** (snappy startup, offline tolerant), then asynchronously call `GET /api/auth/verify` to refresh.
5. On `verify` returning 401, hard-logout: destroy MTP session, clear encrypted permissions file, route back to Intro.
6. Persist all auth-derived data (token, permissions, backend) in a single encrypted file alongside existing MTP data, under `Storage::Account`.

## 3. Non-goals

- UI layer: button disable, menu-item hiding, modal blocking. (Deferred to phase 2.)
- Business API layer: `ApiWrap::sendMessage` / `EditMessage` / `Histories::deleteMessages` / etc. enforcement. (Deferred to phase 3.)
- Periodic or on-focus verify polling. (Deferred to phase 4.)
- Token rotation / expiry tracking. `verify` does not return a new token; the same token is used until 401.
- `GET /api/permissions/hidden-folders`. Separate spec.
- Modal UX for 401 (show "re-login required" dialog). Spec uses silent hard-reset; reuses existing Intro entry path.

## 4. Key Decisions

| # | Decision | Alternative considered |
|---|---|---|
| 1 | Mount permissions on `Main::Account` | `Main::Session` (dies at logout, loses Intro→Session handoff data); `Core::App()` singleton (alien to tdesktop style) |
| 2 | `enum class PermissionKey` with fixed 14 values + `std::array<bool, 14>` backing store | `base::flat_map<QString, bool>` (no compile-time typo check); enum + map hybrid (overengineered — tdesktop cannot enforce a key it has no code for) |
| 3 | Persist to disk + async verify on cold start (selected option 1 of 3) | Force verify before main window (blocks on network); no-persist + async verify (unknown permissions until network responds) |
| 4 | `Storage::Account` encrypted file via `localKey` | Plaintext `QSettings` in `employee_prefs` (tamperable — users could flip bits to bypass client-side checks); standalone self-encrypted file (reinvents tdesktop's existing crypto infrastructure) |
| 5 | Dedicated `class Employee::Permissions` exposing both `has()` (non-reactive) and `value()` (rpl producer) | All methods on `Main::Account` (bloats `main_account.h`); only `has()` without rpl (forces a revisit in phase 2 for UI disable) |
| 6 | Verify fires once on cold start only; 401 → hard reset to Intro; other errors → keep disk + log | Periodic polling (out of scope); soft-degrade to all-false (leaves MTP session alive with invalid token — semantically muddy); modal dialog (interrupts UX, awkward in Intro transition) |

## 5. Architecture

```
                    POST /api/auth/login                      GET /api/auth/verify
                          │                                            │
                          ▼                                            ▼
┌────────────────────────────────────────────────┐       ┌──────────────────────────┐
│ Intro::Employee                                │       │ Employee::VerifyClient   │
│   AuthClient (existing)                        │       │   (new, QNetworkAM)      │
│   ├ ParseAuthResponse: returns token +         │       │   GET with Bearer token  │
│   │  std::array<bool, 14> permissions          │       │   200 → permissions      │
│   └ AuthSuccess includes token + permissions   │       │   401 → InvalidToken     │
└────────────────┬───────────────────────────────┘       │   other → Network|Server │
                 │                                        └────────────┬─────────────┘
                 ▼                                                     │
┌─────────────────────────────────────────────────────────────────────┼─────────┐
│ Main::Account                                                        │         │
│   applyEmployeeBootstrap(dcId, key, userId, token, perms, backend)   │         │
│   employeePermissions() const &    → Employee::Permissions           │         │
│   start() tail: readEmployeeAuth() → apply() → kickOffVerify() ──────┘         │
│   applyEmployeeReset(): clear memory + disk + MTP session                       │
└────────┬─────────────────────────────────────────────┬──────────────────────────┘
         │ write/read encrypted file                   │ query / subscribe
         ▼                                             ▼
┌─────────────────────────┐                ┌──────────────────────────────────┐
│ Storage::Account        │                │ Employee::Permissions            │
│   new fileKey:          │                │   rpl::variable<std::array<     │
│     _employeeAuthKey    │◄──── apply() ──│                 bool, 14>>      │
│   writeEmployeeAuth()   │                │   has(PermissionKey) const      │
│   readEmployeeAuth()    │                │   value(PermissionKey) producer │
│   clearEmployeeAuth()   │                │   changes() producer            │
│                         │                │   token() / authorized()        │
│ Encrypted with existing │                │   apply(values, token)          │
│ localKey                │                │   clear()                        │
└─────────────────────────┘                └──────────────────────────────────┘

All new code gated by #ifdef TDESKTOP_EMPLOYEE_MODE, matching the login system.
```

## 6. Components

### 6.1 New files

| File | Purpose |
|---|---|
| `intro/employee/employee_permissions.h` | `enum class PermissionKey` (14 values) + `class Permissions` + string↔enum mapping table declaration |
| `intro/employee/employee_permissions.cpp` | `Permissions` implementation; `JsonKeyToPermission()` static map |
| `intro/employee/employee_verify.h` | `VerifyClient` class + `VerifyResult = std::variant<VerifySuccess, VerifyFailure>` |
| `intro/employee/employee_verify.cpp` | `VerifyClient` implementation; 401 routing |
| `intro/employee/employee_auth_storage.h` | `EmployeeAuthSnapshot` struct + `Serialize`/`Deserialize` free functions |
| `intro/employee/employee_auth_storage.cpp` | Binary serialization (QDataStream) with version tag |

### 6.2 Modified files

| File | Change |
|---|---|
| `intro/employee/employee_auth.h` | `AuthSuccess` adds `QString token` and `std::array<bool, 14> permissions` |
| `intro/employee/employee_auth.cpp` | `ParseAuthResponse` reads `token` (string, required) and `permissions` (object → 14 bool); unknown keys logged and dropped; missing known keys = false |
| `main/main_account.h` | `applyEmployeeBootstrap` signature extended with `token`, `permissions`, `backend`; new `employeePermissions()` accessor; new private members |
| `main/main_account.cpp` | Bootstrap writes disk; `start()` reads disk + kicks verify; `applyEmployeeReset` clears memory + disk; verify callback handler |
| `storage/storage_account.h/.cpp` | New `_employeeAuthKey` fileKey; `writeEmployeeAuth(bytes)`, `readEmployeeAuth() → QByteArray`, `clearEmployeeAuth()` methods using existing encrypted-file helpers |

### 6.3 Key interfaces

**`employee_permissions.h`**

```cpp
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

class Permissions final {
public:
    Permissions();
    ~Permissions();

    [[nodiscard]] bool has(PermissionKey key) const;
    [[nodiscard]] rpl::producer<bool> value(PermissionKey key) const;
    [[nodiscard]] rpl::producer<> changes() const;
    [[nodiscard]] const QString &token() const;
    [[nodiscard]] bool authorized() const;

    void apply(
        std::array<bool, kPermissionCount> values,
        QString token);
    void clear();

private:
    rpl::variable<std::array<bool, kPermissionCount>> _values;
    QString _token;
};

[[nodiscard]] const base::flat_map<QString, PermissionKey>
    &JsonKeyToPermission();

} // namespace Intro::Employee
```

**`employee_verify.h`**

```cpp
namespace Intro::Employee {

struct VerifySuccess {
    std::array<bool, kPermissionCount> permissions;
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

} // namespace Intro::Employee
```

**`employee_auth_storage.h`**

```cpp
namespace Intro::Employee {

struct EmployeeAuthSnapshot {
    QString token;
    std::array<bool, kPermissionCount> permissions{};
    BackendType backend = BackendType::Customer;
};

[[nodiscard]] QByteArray SerializeEmployeeAuth(
    const EmployeeAuthSnapshot &snap);

[[nodiscard]] std::optional<EmployeeAuthSnapshot> DeserializeEmployeeAuth(
    const QByteArray &bytes);

} // namespace Intro::Employee
```

Binary format (version 1):

```
u32  magic   = 'EMPA'
u32  version = 1
u32  tokenLen
u8[] tokenUtf8
u16  permissionBits (bit N = permissions[N])
u32  backend      (BackendType enum value)
```

Invalid magic, unknown version, or truncated input → `DeserializeEmployeeAuth` returns `nullopt`.

**`main_account.h` additions** (inside existing `#ifdef TDESKTOP_EMPLOYEE_MODE`)

```cpp
[[nodiscard]] const Intro::Employee::Permissions &
    employeePermissions() const;

void applyEmployeeBootstrap(
    MTP::DcId dcId,
    std::shared_ptr<MTP::AuthKey> key,
    UserId userId,
    QString token,
    std::array<bool, Intro::Employee::kPermissionCount> permissions,
    BackendType backend);

void applyEmployeeReset();

private:
    std::unique_ptr<Intro::Employee::Permissions> _employeePermissions;
    std::unique_ptr<Intro::Employee::VerifyClient> _employeeVerify;
    BackendType _employeeBackend = BackendType::Customer;

    void kickOffVerifyIfAuthorized();
    void onVerifyResult(Intro::Employee::VerifyResult result);
```

### 6.4 Dependencies

```
main_account.h ──┬──► employee_permissions.h  (lightweight: enum + class header only)
                 └──► employee_verify.h       (lightweight: struct + class header only)

employee_login_step.cpp ──► main_account.h (triggers bootstrap, unchanged include)

storage_account.cpp ──► employee_auth_storage.h  (serialization only)

Future business call sites (phase 3) ──► main_account.h → employee_permissions.h
```

Business callers only need the two light headers; `employee_auth.h` and `employee_verify.h` are never pulled through.

## 7. Data Flow

### 7.1 First login

```
EmployeeLoginStep::startLogin()
    → AuthClient POST /api/auth/login
    → ParseAuthResponse:
         tdesktopSession.{dcId, authKeyHex, userId}   (existing)
         token                                         (new; required; missing → BadJson)
         permissions object → std::array<bool, 14>    (new; missing → all false, log)
    → AuthSuccess { dcId, authKey, userId, token, permissions }
    → onLoginSuccess
    → account->applyEmployeeBootstrap(
           dcId, key, userId, token, permissions, _chosenBackend)
           ├ _employeePermissions->apply(permissions, token)
           ├ _employeeBackend = backend
           ├ Storage::Account::writeEmployeeAuth(
                 Serialize({ token, permissions, backend }))
           └ MTP start (existing)
    → users.GetUsers → Step::finish(user) → main window
```

### 7.2 Cold start with persisted account

```
Main::Domain::start()
    → Account::start()
        ├ MTP read + start (existing)
        ├ Storage::Account::readEmployeeAuth() → QByteArray
        │   ├ empty → _employeePermissions stays default;
        │   │         if MTP data is present → this is the "first boot after
        │   │         feature ships" case; trigger hard logout (§7.5)
        │   ├ Deserialize fail → trigger hard logout (§7.5)
        │   └ Deserialize OK → snap:
        │       _employeePermissions->apply(snap.permissions, snap.token)
        │       _employeeBackend = snap.backend
        └ kickOffVerifyIfAuthorized():
            if !_employeePermissions->authorized() return;
            _employeeVerify->verify(_employeeBackend, token, crl::guard(this, [this](r) { onVerifyResult(r); }))

Main window renders immediately using disk permissions.

Later, verify callback onVerifyResult:
    VerifySuccess { permissions: newArr }:
        _employeePermissions->apply(newArr, sameToken)
        Storage::Account::writeEmployeeAuth(Serialize({ token, newArr, backend }))
        rpl subscribers notified.
    VerifyFailure { InvalidToken }:
        triggerHardLogout()  — see §7.5 for the mechanism.
    VerifyFailure { Network | Server | BadJson }:
        LOG("Employee: verify failed kind=..."); keep disk + memory unchanged
```

### 7.3 Logout (user-initiated, existing flow)

```
Account::logOut()
    ├ applyEmployeeReset():
    │   ├ _employeeVerify->cancel()  (abort in-flight verify)
    │   ├ _employeePermissions->clear()
    │   └ Storage::Account::clearEmployeeAuth()
    └ MTP teardown (existing)
```

### 7.4 Concurrency safety

| Race | Mitigation |
|---|---|
| verify callback fires after account destruction | `crl::guard(this, lambda)` wrap on every `verify` done callback |
| logout while verify in flight | `applyEmployeeReset` calls `_employeeVerify->cancel()` first; cancel does `_reply->disconnect()` + `_reply.clear()` (pattern copied from `AuthClient::cancel`) |
| re-login (new bootstrap) while previous verify in flight | `applyEmployeeBootstrap` calls `_employeeVerify->cancel()` before apply; new verify fires with fresh token |
| Double-click login during slow network | Already guarded by existing `_bootstrapInFlight` flag in `EmployeeLoginStep` |
| Corrupted disk file on cold start | See §7.5 hard logout; treated same as 401 |

### 7.5 Hard logout mechanism (401 and disk corruption)

401 from verify and a corrupted / missing permissions file both require forcing the user back to Intro. tdesktop already has one entry point for this — the user-initiated logout that runs through `Main::Domain`. The implementation plan must:

1. Identify the minimum-surface call that (a) destroys the MTP session and (b) causes `Main::Domain` / window layer to switch back to the Intro widget.
2. Candidates: reuse `Account::logOut()`; or add a new `Account::forceEmployeeLogout()` that mirrors `logOut`'s domain-notification side effects without the server-side destroy call (since a 401 token can't be used to destroy anyway).
3. Reject any approach that only clears local state — the window must transition, otherwise the user sees a permission-less main window with no way out.

The spec does not prescribe which; the writing-plans step chooses based on code inspection. But the plan **must** produce a working Intro transition, verified in manual test matrix scenarios 4, 11, and 13.

## 8. Error Handling

### 8.1 Parse errors

| Condition | Behavior | UI |
|---|---|---|
| Login response missing `token` | `AuthFailure::BadJson` | Red error: existing `lng_employee_err_bad_json` |
| Login response `permissions` not an object | Log; permissions → all false; token still accepted | No UI error; user enters with zero permissions |
| Login response permission value not bool | Log; that key → false | No UI error |
| Login response unknown permission key | Log; dropped | No UI error |
| Verify response JSON malformed | `VerifyFailure::BadJson`; disk preserved | No UI (silent log) |
| Disk file corrupted / wrong version | `DeserializeEmployeeAuth` → `nullopt`; trigger hard logout (§7.5) on the event loop tick after `Account::start()` returns, since MTP is up but token is lost — only re-login can recover | No UI; user sees Intro after brief flash |
| Disk file missing (first cold start after this feature ships to existing employees) | Same as corrupted: hard logout, force re-login (migration — see §11) | No UI; user sees Intro |

### 8.2 Network errors

| Condition | Login behavior | Verify behavior |
|---|---|---|
| Network / DNS / TLS failure | Red error `Kind::Network` | `Kind::Network`; disk preserved; no retry |
| HTTP 5xx | `Kind::Http5xx`; use backend `error` text or fallback | `Kind::Server`; disk preserved |
| HTTP 401 | Existing 4xx path | `Kind::InvalidToken` → hard reset to Intro |
| HTTP other 4xx | Existing 4xx path | `Kind::Server`; disk preserved |

### 8.3 Missing / unknown permission keys (semantic table)

| Backend response shape | `has(PermissionKey::X)` result |
|---|---|
| `permissions.<jsonKey> = true` | `true` |
| `permissions.<jsonKey> = false` | `false` |
| `permissions.<jsonKey>` missing | `false` (default) |
| Entire `permissions` field missing | `false` for all 14 keys |
| `permissions.newKey = true` (tdesktop does not know `newKey`) | No effect; `has(X)` only defined on enum members |
| Permission value is a number or string | Treated as `false`; log |

## 9. Testing

### 9.1 Unit tests

tdesktop has no established test harness in this repository; the writing-plans step confirms and either wires up a minimal one or defers unit tests to manual §9.2 coverage. If a harness is added, it must be gated by `TDESKTOP_EMPLOYEE_MODE` to avoid growing the upstream surface.

| Target | Cases |
|---|---|
| `ParseAuthResponse` (extended) | (a) complete response → token + permissions populated correctly; (b) token missing → `BadJson`; (c) permissions missing → all false + log; (d) unknown key → dropped + log; (e) non-bool value → that key false; (f) 401 path unchanged from existing behavior |
| `SerializeEmployeeAuth` / `DeserializeEmployeeAuth` | (a) round-trip equality; (b) corrupted magic → `nullopt`; (c) unknown version → `nullopt`; (d) truncated input → `nullopt`; (e) empty token → round-trip preserves empty |
| `Permissions::apply` + `has` + `value` + `clear` | (a) default state = all false, `authorized() == false`; (b) apply → `has` reflects values, `authorized() == !token.isEmpty()`; (c) subscribe `value(X)` then apply with changed X → callback fires; (d) clear → token empty, all false, `authorized() == false` |

### 9.2 Manual integration matrix (post-compile)

| # | Scenario | Expected |
|---|---|---|
| 1 | New login with backend returning all permissions `true` | Main window opens; `employeePermissions().has(MsgSend)` returns `true` in debug log |
| 2 | Cold restart after (1) | Disk read sets permissions immediately; verify async log `permissions refreshed`; no UI flicker |
| 3 | Backend flips `msg.send` to `false` between sessions, cold restart | Disk still shows `true` at launch; seconds later verify overrides to `false`; disk rewritten |
| 4 | Backend revokes token, cold restart | Verify returns 401 → hard reset; encrypted file cleared; user lands on Intro |
| 5 | Cold restart while offline | Disk permissions available; verify fails `Network`; disk preserved; user usable with cached perms |
| 6 | Login response with `permissions` field missing | Main window opens; all 14 keys return `false`; log entry present |
| 7 | Backend adds new key `msg.pin` (tdesktop unaware) | Log `unknown key msg.pin`; other permissions unaffected |
| 8 | Manual logout | Encrypted file cleared; cold restart lands on Intro |
| 9 | Login, logout, login (rapid) | Second bootstrap cancels first verify; second verify uses fresh token |
| 10 | Logout during in-flight verify | Verify callback suppressed (reply disconnected) |
| 11 | Disk file corruption (manually edit bytes) | Log parse fail; hard logout triggered; user lands on Intro and can re-login |
| 12 | Backend sends non-bool value for a key | Log entry; that key treated as `false`; other keys parsed |
| 13 | First cold start after deploying this feature to a machine that already had an employee login under v2 (no permissions file yet) | Log `empty employee auth file`; hard logout; user lands on Intro and re-logs in |

## 10. Out of Scope (Phase 2+ Preview)

This spec deliberately does not implement enforcement. Once landed, the query API will be consumed by:

- **Phase 2 (UI disable)**: ~10 components in `boxes/`, `history/`, `dialogs/` subscribing to `permissions.value(X)` for button `enabled` state
- **Phase 3 (Business API interception)**: ~12 early-return checks in `ApiWrap`, `EditMessage`, `Histories`, `ChatParticipants`, `ChatFilters`, `BlockedPeers`, `AddGroupBox`, `EditContactBox`
- **Phase 4 (Active refresh)**: periodic verify, on-focus verify, `/api/permissions/hidden-folders` integration

The API shape (`has()` + `value()`) supports both reactive and non-reactive consumers without change.

## 11. Risks & Open Questions

| Risk | Mitigation |
|---|---|
| Binary format version drift between releases | Version field in header; unknown version → `nullopt` (treated as empty); user re-logs in |
| Verify runs before MTP is fully up | Verify uses QNetworkAccessManager (independent of MTP); no dependency |
| Concurrent disk writes (bootstrap during verify callback) | All disk writes sequenced through Account's single-threaded event loop; no locking needed |
| Token leaked via log | `LOG` never logs token contents; only length or presence |
| Future backend token rotation | `verify` response shape is fixed to permissions-only; if backend later returns new token, `VerifySuccess` can be extended additively |
| **Migration: existing employee-mode users have no permissions file** | Currently `employee-login-v2` (already merged to customization) writes no permissions disk file. First cold start after this spec ships will hit the "disk file missing but MTP present" branch and force re-login. Acceptable because employee mode has not been released to production yet. The writing-plans step must flag this in the rollout checklist so any test accounts are re-logged in deliberately |
| Backend `permissions` key names drift from the 14 documented keys | `JsonKeyToPermission()` is the single source of truth for mapping backend strings to enum. If backend renames a key (e.g., `msg.send` → `message.send`), the entry in this table changes; no other code moves. New keys are ignored (logged); removed keys fall back to `false` by default |
| `BackendInfoFor` no longer recognizes a persisted `BackendType` value | Possible if the `BackendType` enum shrinks between versions. Deserialize guards by clamping out-of-range values to `Customer`; user may need to re-login if the server is on a decommissioned backend |
