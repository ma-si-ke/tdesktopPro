# Set Device Name From Server-Supplied employee.name on Login Design Spec

**Date:** 2026-04-25
**Status:** Approved
**Depends on:** employee login flow (`0d2f6fe`, `733b59c`, `446574e`); custom device-model setting (`Core::App().settings().setCustomDeviceModel`)
**Related:** `hide-user-username-v1`, `hide-user-phone-v1` — same anti-leak / employee accountability theme

---

## 1. Goal

When the employee logs in successfully, automatically set `Core::App().settings().customDeviceModel()` to the `employee.name` field from the login response, so the "Active Sessions" list (admin-facing and self-facing) shows the real employee name instead of the machine's BIOS product name or `"Desktop"`.

**Success criteria:**
1. After a successful employee login, the next `MTPInitConnection` carries `device_model = <employee.name>`.
2. Server-side authorization record for this session shows `device_model = <employee.name>`.
3. Active Sessions UI on this client (and any other authorized client of the same Telegram user) displays `<employee.name>` as the device name for this session.
4. If the employee re-logs in after admin changes their name in the back-end, the new name takes effect at next login.
5. Empty / missing / non-string `employee.name` does NOT clobber an existing `customDeviceModel`.
6. Cold-start / auto-login (cached authKey, no fresh login) preserves the last-written name without churn.
7. The change is gated by `TDESKTOP_EMPLOYEE_MODE` (existing gate on `employee_auth.{h,cpp}` covers this naturally).

**Non-goals:**
- Real-time sync of admin name-changes (handled at next login only; `/api/auth/verify` not touched).
- Persisting `employee.name` in tdata's employee-auth snapshot (the value lives in `customDeviceModel` which already persists in core_settings).
- Clearing `customDeviceModel` on logout / `applyEmployeeReset`.
- UI to display the auto-set name in the login flow.
- Preventing the employee from manually editing device name in Settings → Active Sessions (will still work — but next login overwrites).
- Truncating long names client-side (Telegram server enforces its own limits).

---

## 2. Approach

Thread the new field through the existing employee-login plumbing in 5 surgical edits, then call `setCustomDeviceModel` at the top of `applyEmployeeBootstrap` (before the new MTP::Instance is constructed).

### Components

**a) `intro/employee/employee_auth.h:24-30` — extend `AuthSuccess`**

```cpp
struct AuthSuccess {
    MTP::DcId dcId = 0;
    QByteArray authKey;
    UserId userId = 0;
    QString employeeName;            // ← NEW: from response root.employee.name
    QString token;
    PermissionValues permissions{};
};
```

Field placement after `userId` keeps "identity-related" fields together; before `token` to leave the existing `token`/`permissions` pair untouched relative to each other.

**b) `intro/employee/employee_auth.cpp:184-190` — extract & populate**

In `ParseAuthResponse`, immediately before the existing `return AuthSuccess{ ... }`, read:

```cpp
const auto employeeName = root.value(u"employee"_q)
    .toObject()
    .value(u"name"_q)
    .toString();
```

`QJsonValue::toString()` returns empty string for missing/null/non-string. Then include it in the return:

```cpp
return AuthSuccess{
    MTP::DcId(dcId),
    keyBytes,
    userId,
    employeeName,
    token,
    permissions,
};
```

**c) `intro/employee/employee_login_step.cpp:237-243` — pass through**

```cpp
account().applyEmployeeBootstrap(
    result.dcId,
    std::move(key),
    result.userId,
    result.employeeName,    // ← NEW
    result.token,
    result.permissions,
    _chosenBackend);
```

**d) `main/main_account.h` — extend `applyEmployeeBootstrap` signature**

Insert `QString employeeName,` after `userId` and before `token`. Public API only used by employee-login flow; no other call sites exist (verified in implementation).

**e) `main/main_account.cpp` — apply at the top of `applyEmployeeBootstrap`**

Right after the entry `Expects(...)` block and `_employeeVerify->cancel()`, before `_employeeBackend = backend;`:

```cpp
if (!employeeName.isEmpty()) {
    Core::App().settings().setCustomDeviceModel(employeeName);
    Core::App().saveSettingsDelayed();
}
```

This must run **before `startMtp(...)`** because `MTP::Instance::Private::start` reads `customDeviceModel` once at construction (`mtp_instance.cpp:336`) and uses it for the very first `MTPInitConnection`. Placing the call at the top of the function body satisfies this ordering naturally.

### Integration with existing flow

```
employee_login_step.cpp:200  injectAndFetchSelf(result)
   └─ applyEmployeeBootstrap(..., result.employeeName, ...)
        ├─ if (!employeeName.isEmpty()) setCustomDeviceModel(...)   ← NEW
        ├─ saveSettingsDelayed()                                    ← NEW
        ├─ _employeeBackend = backend
        ├─ _employeePermissions->apply(...)
        ├─ writeEmployeeAuth(...)
        ├─ _mtpFields = { dcId, key }
        └─ startMtp(config)   ← reads customDeviceModel during construction
              └─ first MTPInitConnection sent with device_model = employeeName
```

---

## 3. Data Flow

```
Server JSON:
  { ..., "employee": { "name": "张三", ... }, ... }

Parse  →  AuthSuccess.employeeName = "张三"
   ↓
employee_login_step  →  applyEmployeeBootstrap(..., employeeName="张三", ...)
   ↓
core_settings._customDeviceModel.set("张三")
   ↓ (rpl propagates to existing MTP::Instance, but new instance reads it at construction)
saveSettingsDelayed()  →  serialized to tdata along with all other settings
   ↓
startMtp() builds new MTP::Instance
   ↓ Instance reads customDeviceModel == "张三"
   ↓
session_private.cpp  →  MTPInitConnection { device_model = "张三", ... }
   ↓
Telegram server stores in authorization record
   ↓
api_authorizations.cpp:59  Active Sessions UI  →  displays "张三"
```

---

## 4. What Is Not Changed

- `Platform::DeviceModelPretty()` and BIOS-reading code — untouched. Still the fallback if `customDeviceModel` is empty (e.g., before first employee login).
- `Settings → Active Sessions → device name input field` — still functional. Manual edits work for the duration of the session, but next login overwrites them. Acceptable per design A choice.
- `/api/auth/verify` endpoint and `VerifyClient` — untouched. Admin name changes propagate at next login, not via verify.
- `applyEmployeeReset` (logout) — does NOT clear `customDeviceModel`. Next login overwrites; no one sees stale name in offline state anyway.
- `AuthSnapshot` serialization (`serialize_employee.cpp`) — does NOT carry `employeeName`. Persistence already handled by `customDeviceModel` in core_settings.
- Any non-employee build (`#ifdef TDESKTOP_EMPLOYEE_MODE` not defined) — naturally untouched (`employee_auth.h` and `applyEmployeeBootstrap` are both inside that gate).

---

## 5. Edge Cases

| Scenario | Behavior |
|---|---|
| Server omits `employee` key entirely | `root.value("employee").toObject()` is empty object → `.value("name").toString()` is `""` → skip write, preserve existing `customDeviceModel`. |
| Server returns `"employee": null` | Same as above (`toObject()` on a null QJsonValue yields empty object). |
| Server returns `"employee": { "name": null }` | `toString()` of a null is `""` → skip write. |
| Server returns `"employee": { "name": 12345 }` | `toString()` of a number-typed value is `""` → skip write. |
| Server returns valid empty string `"name": ""` | `!isEmpty()` is false → skip write. |
| Long name (e.g. >32 chars) | Written as-is. `MTP::Instance` propagates as `MTP_string`. Telegram server may truncate or reject — same behavior as if user typed a long name in Settings. Not our concern. |
| Name contains special chars / non-Latin | UTF-8 transmitted by Qt; server stores as-is. Active Sessions UI renders correctly (already supports any device-model string). |
| Two employees share one machine, take turns | Each login overwrites `customDeviceModel`; session A always sees session A's name in MTPInitConnection. |
| Cold start with cached auth (no fresh login) | `applyEmployeeBootstrap` not called; `customDeviceModel` already persisted in core_settings; Telegram session resumes with the stored name. ✓ |
| Employee manually edits device name in Settings UI mid-session | Edit takes immediate effect (rpl propagation); next login overwrites with `employee.name`. |
| Employee logs out, then a different employee logs in | Reset path doesn't touch `customDeviceModel`; new login immediately overwrites. No stale-name visible window. |
| Login response valid but `employeeName` write throws (impossible — pure setter) | N/A — `setCustomDeviceModel` is a plain setter. |

---

## 6. Testing (smoke)

1. Log in with an account whose server response has `employee.name = "测试员工A"`. Open Settings → Active Sessions on this client → top entry's device name shows **"测试员工A"** (not BIOS product name / "Desktop").
2. Cross-check from another logged-in Telegram client (mobile / web) → "Active Sessions" → desktop session shows **"测试员工A"**.
3. Log out, log in again with the same employee → name persists / re-applies. (Verify by inspecting `tdata/settings*` or Settings → Active Sessions.)
4. Have admin change the back-end name from `"测试员工A"` to `"测试员工B"` → log out → log back in → device name shows **"测试员工B"**.
5. Manually open Settings → Active Sessions → click own device → change name to `"我的电脑"` → save → confirm UI updates → log out → log back in → name reverts to **`employee.name`** (overwrite working as designed).
6. Simulate server response missing `employee` key → log in → device name does NOT change to empty string; if previously set to `"测试员工A"`, it stays `"测试员工A"` (defensive skip).
7. Simulate server response with `"employee": { "name": "" }` → same as above (skip).
8. Verify a non-employee Telegram Desktop build (no `TDESKTOP_EMPLOYEE_MODE`) still compiles and runs — no employee_auth.cpp involvement, no behavior change.

---

## 7. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| `setCustomDeviceModel` called after `startMtp` → first `initConnection` uses old name | Low | Spec mandates "before `startMtp`"; placement at top of `applyEmployeeBootstrap` body enforces this. Verified by reading `mtp_instance.cpp:336` (constructor reads custom model once). |
| Adding parameter to `applyEmployeeBootstrap` breaks an unknown caller | Very low | grep confirms only one call site (`employee_login_step.cpp:237`); compiler will reject any missed site. |
| `saveSettingsDelayed()` deferral makes the value un-persisted at the moment `startMtp` runs | None | `setCustomDeviceModel` writes the in-memory `_customDeviceModel`; `saveSettingsDelayed` is just a disk-flush schedule. MTP::Instance reads the in-memory value, not disk. |
| Changing `AuthSuccess` field order breaks brace-init at the return site | None | Spec keeps brace-init form; compiler will catch a misaligned order. |
| Long / invalid name triggers Qt or Telegram-server error mid-login | Low | Empty/non-string filtered out; long names handled by server (same as manual input). No new failure mode. |
| `core/application.h` / settings include not present in `main_account.cpp` | None | grep confirms `Core::App().settings()` already used elsewhere in the file (existing employee-bootstrap code path). |
| Race: rpl `_customDeviceModelChanges` fires before MTP::Instance subscribes | None | New MTP::Instance reads value at construction (line 336), THEN subscribes (line 337-340). Single-threaded init order on main thread. |
| Future request to NOT overwrite manually-set names | None | A small future change — add a "manually-set" flag in core_settings; not speculative now. |

---

## 8. Implementation Ordering

Single-task plan. 5 file edits applied as one batch (no intermediate compiles), then a single build:

1. Edit `employee_auth.h` — add `employeeName` field to `AuthSuccess` between `userId` and `token`.
2. Edit `employee_auth.cpp` — in `ParseAuthResponse`, extract `root.value("employee").toObject().value("name").toString()` and include it in the returned `AuthSuccess` brace-init.
3. Edit `main_account.h` — add `QString employeeName` parameter to `applyEmployeeBootstrap`, between `userId` and `token`.
4. Edit `main_account.cpp` — match the new signature in the function definition; insert the `if (!employeeName.isEmpty()) { setCustomDeviceModel(...); saveSettingsDelayed(); }` block immediately after `_employeeVerify->cancel();` and before `_employeeBackend = backend;`.
5. Edit `employee_login_step.cpp` — pass `result.employeeName` as the new argument when calling `applyEmployeeBootstrap`.
6. `touch` each modified file (WSL/9P mtime defense — same as prior tasks).
7. Compile (Debug). `cmake --build out --config Debug --target Telegram`.
8. Smoke-test items 1-8 above.
9. Commit on `customization` branch with message `feat(employee): set device name from server-supplied employee.name on login`.
10. Tag `employee-device-name-v1`. Do not push.
