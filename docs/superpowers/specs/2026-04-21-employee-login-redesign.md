# Employee Login Redesign — Design Spec

- **Date**: 2026-04-21
- **Status**: Approved for implementation planning
- **Supersedes**: The previous `employee/` implementation (rolled back on 2026-04-21 because it bypassed tdesktop's Intro framework and crashed by calling `createSession` with a stub `MTPUser`).

## 1. Goals

Replace tdesktop's default phone/SMS/2FA login with a company backend HTTP login, **while reusing tdesktop's own Intro framework as much as possible**. The user provides `username + password`; our backend returns `{ dcId, authKeyHex, userId }`. We then hand off to the exact same code path that the official flow uses after successful SMS auth.

Non-goals:

- Supporting the official phone/SMS/QR login paths in employee builds (they are replaced, not coexisting).
- Any backend contract change. The HTTP API returning `tdesktopSession: { dcId, authKeyHex, userId }` is already deployed.
- Online-status heartbeat, force-kick, or backend-driven logout. These are explicitly out of scope.

## 2. Decisions (from brainstorming)

| # | Decision | Rationale |
|---|---|---|
| 1 | Integrate as a new `Intro::details::Step` subclass | Inherit animations, back/next plumbing, error label style; converges with SMS flow at `Step::finish(user)`. |
| 2 | 4-backend dropdown on top of the login form | Consistent with the country selector on the phone step; remembers last choice. |
| 3 | Trust tdesktop local persistence | After first success, `writeMtpData()` handles authKey storage; subsequent launches skip our form. |
| 4 | Logout clears local state only | No `auth.logOut`, no backend notification. Simplest; avoids having Telegram invalidate the AuthKey. |
| 5 | All errors inline via a red `FlatLabel` beneath inputs | Matches `PHONE_CODE_INVALID` style in `intro_code.cpp`. |
| 6 | Conservative logging with `Employee:` prefix | `LOG()` for failures and coarse success events only; `DEBUG_LOG()` for minor detail. Never log secrets. |
| 7 | Handoff via `users.GetUsers([InputUserSelf])` to fetch real `MTPUser` | Converges with original flow at `Step::finish(user)`. |
| 8 | All changes gated by `TDESKTOP_EMPLOYEE_MODE` compile flag | Keeps upstream diff small; allows building an equivalent-to-upstream tdesktop from the same tree. |

## 3. Architecture

### 3.1 File layout

New (under `Telegram/SourceFiles/intro/employee/`):

| File | Purpose |
|---|---|
| `employee_config.{h,cpp}` | `BackendType` enum, `BackendInfo` struct, host/port constants for the four backends. |
| `employee_auth.{h,cpp}` | `AuthClient`: async `QNetworkAccessManager` wrapper for `POST /api/auth/login`. |
| `employee_login_step.{h,cpp}` | `EmployeeLoginStep` inheriting `Intro::details::Step`. Owns the form and orchestrates the full bootstrap. |
| `employee_prefs.{h,cpp}` | Persist last-used backend and username (not password, not authKey). |

Modified:

| File | Change |
|---|---|
| `intro/intro_widget.cpp` | In `Widget::appendStep()` / entry-point dispatch, when `TDESKTOP_EMPLOYEE_MODE` is defined, route `EnterPoint::Start` and `EnterPoint::Qr` to `EmployeeLoginStep` instead of `StartWidget` / `QrWidget`. |
| `main/main_account.{h,cpp}` | Add `applyEmployeeBootstrap(MTP::DcId, MTP::AuthKeyPtr, UserId)`. Only this method, only behind `#ifdef TDESKTOP_EMPLOYEE_MODE`. |
| `Telegram/CMakeLists.txt` | Register the four new files. Add `Dnsapi` link dep with `/DELAYLOAD:dnsapi.dll` (works around prior LNK2019 from Qt6Network DNS lookup). |
| `Resources/langs/lang.strings` | Add `lng_employee_*` localization keys (see §6). |

### 3.2 Compile flag

`TDESKTOP_EMPLOYEE_MODE` defined in `Telegram/CMakeLists.txt` via `target_compile_definitions(Telegram PRIVATE TDESKTOP_EMPLOYEE_MODE=1)`. Every non-additive code change is wrapped in `#ifdef` / `#endif`. When the flag is undefined, the build is byte-for-byte equivalent to unmodified upstream.

## 4. Components

### 4.1 `Intro::Employee::BackendType` (employee_config.h)

```cpp
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
    int port;
};

[[nodiscard]] const BackendInfo &BackendInfoFor(BackendType type);
[[nodiscard]] std::array<BackendInfo, 4> AllBackends();

} // namespace Intro::Employee
```

Constants (all port 3000, plain HTTP):

| Type | Label | Host |
|---|---|---|
| Customer | 客服 | 101.32.15.201 |
| Staff | 员工号 | 43.154.241.172 |
| Admin | 后台 | 129.226.182.152 |
| Other | 其他 | 43.132.171.63 |

### 4.2 `Intro::Employee::AuthClient` (employee_auth.h)

```cpp
struct AuthSuccess {
    MTP::DcId dcId;
    QByteArray authKey;   // 256 bytes, already hex-decoded
    UserId userId;
};

struct AuthFailure {
    enum class Kind {
        Network, Http4xx, Http5xx, BadJson, AlreadyOnline, NotBound
    };
    Kind kind;
    QString message;      // Pre-localized for display
};

using AuthResult = std::variant<AuthSuccess, AuthFailure>;

class AuthClient final : public QObject {
public:
    explicit AuthClient(QObject *parent = nullptr);

    void login(
        BackendType backend,
        QString username,
        QString password,
        Fn<void(AuthResult)> done);
    void cancel();

private:
    QNetworkAccessManager _nam;
    QNetworkReply *_reply = nullptr;
};
```

Plain HTTP POST to `http://{host}:{port}/api/auth/login` with JSON body `{"username":...,"password":...}`. Response parsing extracts `tdesktopSession.{dcId, authKeyHex, userId}`, validates hex length == 512, decodes to `QByteArray`. Non-2xx or business error codes map to `AuthFailure` with pre-localized message.

### 4.3 `Intro::Employee::EmployeeLoginStep` (employee_login_step.h)

```cpp
class EmployeeLoginStep final : public Intro::details::Step {
public:
    EmployeeLoginStep(
        QWidget *parent,
        not_null<Main::Account*> account,
        not_null<Intro::details::Data*> data);
    ~EmployeeLoginStep();

    void submit() override;
    rpl::producer<QString> nextButtonText() const override;

private:
    void onLoginSuccess(AuthSuccess result);
    void onLoginFailure(AuthFailure result);
    void injectAndFetchSelf(AuthSuccess result);
    void fetchSelf();
    void onSelfLoaded(const MTPUser &user);
    void onSelfFailed(const MTP::Error &err);
    void showError(QString text);
    void lockInputs(bool locked);
    void resetAfterFailure();

    object_ptr<Ui::InputField> _username;
    object_ptr<Ui::PasswordInput> _password;
    object_ptr<Ui::AbstractButton> _backendDropdown;  // Button that opens a Ui::PopupMenu with 4 items; exact widget type to match whatever intro/* already uses for similar pickers (to be confirmed at implementation time by grepping intro_phone.cpp and neighbors).
    object_ptr<Ui::FlatLabel> _errorLabel;            // Styled via st::introError or closest existing red-label style; confirm in intro.style during implementation.

    std::unique_ptr<AuthClient> _auth;
    BackendType _chosenBackend = BackendType::Customer;

    mtpRequestId _getSelfRequestId = 0;
    base::Timer _mtpConnectTimeout;
    rpl::lifetime _mtpWatch;
};
```

Notes:

- Inherits `Step` to get animation + next-button plumbing for free.
- Does **not** implement `createSession`. At the end of the success path it calls inherited `Step::finish(user)` — the same method SMS verification uses.
- Password input uses the `Q_SIGNAL submitted(Qt::KeyboardModifiers)` pattern (PasswordInput is not `RpWidget`).

### 4.4 `Intro::Employee::Prefs` (employee_prefs.h)

```cpp
namespace Intro::Employee::Prefs {

[[nodiscard]] BackendType LastBackend();   // default Customer
void SetLastBackend(BackendType b);

[[nodiscard]] QString LastUsername();
void SetLastUsername(QString name);

} // namespace Intro::Employee::Prefs
```

Stored in `Core::App().settings()` (existing settings singleton). **Never** store password or authKey.

### 4.5 `Main::Account::applyEmployeeBootstrap` and `applyEmployeeReset`

Two new methods on `Main::Account`, both gated by `#ifdef TDESKTOP_EMPLOYEE_MODE`. No other existing methods are modified.

```cpp
void applyEmployeeBootstrap(
    MTP::DcId dcId,
    std::shared_ptr<MTP::AuthKey> key,
    UserId userId);

void applyEmployeeReset();
```

`applyEmployeeBootstrap` internals:

1. `Ensures(!_mtp)` — must be called before any MTP start, and only once per `Account` lifetime (until `applyEmployeeReset` clears state).
2. Sets `_mtpFields.mainDcId = dcId`.
3. Sets `_mtpFields.keys = { key }`.
4. Sets `_sessionUserId = userId`.
5. Calls `startMtp(MTP::Config)` with the production fallback config.

`applyEmployeeReset` internals (undoes bootstrap after a failed mid-flight handshake so the user can retry):

1. If `_mtp` is set, `base::take(_mtp)` and let it destruct.
2. Clears `_mtpFields.keys`, resets `_mtpFields.mainDcId` to default.
3. Clears `_sessionUserId`.

These methods are **additive only** — `startMtp`, `createSession`, `logOut`, and other existing `Main::Account` members are untouched.

## 5. Data flow

```
Stage A  (UI submit)
  EmployeeLoginStep::submit
    validate non-empty
    lockInputs(true); clear error label
    _auth->login(backend, user, pass, done-cb)
    LOG("Employee: login attempt backend=%1 user=%2")
                                        ^ username ok to log; password not

Stage B  (HTTP callback)
  onLoginFailure:
    LOG("Employee: login failed kind=%1")
    lockInputs(false); showError(f.message)
  onLoginSuccess:
    LOG("Employee: login ok dcId=%1 userId=%2")
    injectAndFetchSelf(s)

Stage C  (AuthKey injection)
  account->applyEmployeeBootstrap(s.dcId, makeKey(s), s.userId)
  _mtpWatch := account->mtpMainSessionValue()
      | rpl::start_with_next(..., _mtpWatch)
  _mtpConnectTimeout.callOnce(15_000)

Stage D  (fetch real user)
  On first non-null MTP instance: fetchSelf()
  _getSelfRequestId = api().request(
      MTPusers_GetUsers(MTP_vector<MTPInputUser>(1, MTP_inputUserSelf())))
    .done  -> onSelfLoaded(users.v[0])
    .fail  -> onSelfFailed(err)
  On timeout: onSelfFailed(synthetic timeout error)

Stage E  (converge with original flow)
  onSelfLoaded(user):
    Prefs::SetLastBackend(chosenBackend)
    Prefs::SetLastUsername(username)
    LOG("Employee: bootstrap complete")
    finish(user);  // Step::finish — same entrypoint SMS uses
```

`Step::finish(user)` handles: dialog filters fetch, `createSession(user, settings)`, `writeMtpData()`, `Local::sync()`, `setupMain()`.

## 6. Error handling

### 6.1 Failure matrix

| Source | Condition | User-visible (red label) | Log |
|---|---|---|---|
| HTTP network | DNS/timeout/SSL/connect | `lng_employee_err_network` | `LOG("Employee: network error code=%1")` |
| HTTP 4xx | 401/403 | `lng_employee_err_auth` | `LOG("Employee: auth denied http=%1")` |
| HTTP 5xx | 500+ | `lng_employee_err_server` | `LOG("Employee: server error http=%1")` |
| Business | `ALREADY_ONLINE` | `lng_employee_err_already` | `LOG("Employee: already online")` |
| Business | `NOT_BOUND` | `lng_employee_err_not_bound` | `LOG("Employee: not bound")` |
| Parse | bad JSON / missing field | `lng_employee_err_bad_json` | `LOG("Employee: bad json")` |
| Parse | authKey hex length != 512 | `lng_employee_err_bad_json` | `LOG("Employee: authkey length invalid")` |
| MTP timeout | 15s since inject, no connect | `lng_employee_err_mtp_timeout` | `LOG("Employee: MTP connect timeout dcId=%1")` |
| MTP fail | `users.GetUsers` error | see §6.2 | see §6.2 |
| Local | empty username/password | `lng_employee_err_empty` | (not logged) |

### 6.2 `users.GetUsers` failure policy (always reset)

On any `users.GetUsers` failure:

1. Cancel outstanding request.
2. Call `account->applyEmployeeReset()` (additive, `#ifdef`-gated): drops `_mtp`, clears `_mtpFields.keys`, clears `_sessionUserId`. Mirror-image of `applyEmployeeBootstrap`.
3. `lockInputs(false)`.
4. Show error; username/password remain in the form (no field clearing on failure).

Special case: if `err.type()` starts with `FLOOD_WAIT_`, parse the seconds suffix, disable the login button for that many seconds, and show `"请 %1 秒后重试"` (`lng_employee_err_flood`) with a countdown. This is the only branching.

### 6.3 Invariants

- Any failure path **must** unlock inputs.
- Any failure after `applyEmployeeBootstrap` **must** call `applyEmployeeReset` before allowing retry.
- `EmployeeLoginStep` destructor cancels in-flight HTTP and MTP requests.

## 7. Logging

### 7.1 Rules

- Header: `#include "base/debug_log.h"`.
- Use `LOG((...))` for attempt start, success, failure, and bootstrap-complete. Use `DEBUG_LOG((...))` sparingly for intermediate states only (e.g., "MTP instance ready, issuing users.GetUsers").
- Every line is prefixed `"Employee: "` — matches the `"MTP Info: "` / `"API Error: "` convention from `logs.cpp`.
- Qt `.arg()` formatting only.

### 7.2 Safe to log

- Username (after successful validation), `dcId`, `userId.bare`, HTTP status code, `MTP::Error::type()` string, byte lengths.

### 7.3 Never log

- AuthKey bytes, authKey hex, password, full HTTP response body.

### 7.4 Budget

A single login attempt emits **≤ 10 `LOG` lines**, typically 3 (attempt / success / bootstrap complete). Grep-verifiable after testing (§9).

## 8. Localization (lang.strings additions)

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

## 9. Testing

Manual checklist run each iteration (tdesktop has no UI-test harness):

| # | Scenario | Trigger | Expected |
|---|---|---|---|
| 1 | Happy path | Customer backend, `kaka/123123` | Main window; log has `Employee: login ok` + `Employee: bootstrap complete` |
| 2 | Wrong password | Bad creds | Red label "用户名或密码错误"; button re-clickable |
| 3 | Backend unreachable | Stop backend | Red label "无法连接到服务器" |
| 4 | Backend switch | Dropdown to Staff, relaunch | Dropdown remembers Staff |
| 5 | Empty input | Blank fields + click | Red label, no HTTP request |
| 6 | Enter key | Password field Enter | Same as button click |
| 7 | `ALREADY_ONLINE` | Login same account in parallel | Red label "该账号已在其他设备登录" |
| 8 | `NOT_BOUND` | Backend returns it | Red label "账号未绑定 Telegram" |
| 9 | Auto-resume | Login → quit → relaunch | Main window, no form shown |
| 10 | Logout returns to form | Main menu → logout | Form shown; tdata cleared; username/backend prefilled |
| 11 | MTP timeout | Pull network cable after HTTP success | After 15s: red label "连接 Telegram 超时" |
| 12 | Recovery after failure | Scenario 3 → restore network → click again | Login succeeds |
| 13 | Log hygiene | `grep Employee: log.txt` | Only `Employee:` lines; no password / authKey / hex of length ≥ 256 |

Secondary checks:

- Compile-flag equivalence: build with `TDESKTOP_EMPLOYEE_MODE` undefined → resulting binary behaves as upstream tdesktop (shows phone step, SMS verification works end-to-end).
- `applyEmployeeBootstrap` `Ensures()` check fires (assertion) if called twice without an intervening `applyEmployeeReset`.
- On successful login, add a temporary `DEBUG_LOG` at `applyEmployeeBootstrap` entry and at `Step::finish(user)` capture point logging `reinterpret_cast<uintptr_t>(_mtp.get())`. Verify both log lines print the same pointer value — evidence that the instance is not destroyed and rebuilt mid-flow. Remove the temporary logs once verified.

## 10. Open questions / future work

Not in scope for this spec but worth recording:

- Online-status heartbeat to the backend (decision C from brainstorming Q3, deferred).
- `/api/auth/logout` backend call on client logout (decision B from brainstorming Q4, deferred).
- Force-kick on `ALREADY_ONLINE` (decision B from brainstorming Q5, deferred; needs backend support).
- Config-driven backend list instead of compiled constants.
