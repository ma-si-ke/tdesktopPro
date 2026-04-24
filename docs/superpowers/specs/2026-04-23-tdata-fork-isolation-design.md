# tdata Fork Isolation Design Spec

**Date:** 2026-04-23
**Status:** Approved
**Depends on:** none (independent one-function hardening)
**Threat model:** Non-expert office employee. Has file system access. Can download vanilla Telegram Desktop from telegram.org. Cannot reverse-engineer binaries, extract embedded constants, compile custom builds, or derive keys from source.

---

## 1. Goal

Prevent an employee from bypassing this fork's customizations (UI disable layer, notification suppression, service-user hiding, username hiding, employee permission system, etc.) by copying their `tdata/` directory into a vanilla Telegram Desktop install and continuing to use the account without our controls.

**Success criteria:**
1. Fresh install of this fork writes tdata that only this fork can decrypt.
2. Vanilla Telegram Desktop pointed at (or given a copy of) this fork's tdata fails to decrypt — shown as corrupt / treated as fresh install → vanilla offers login-from-scratch.
3. Our fork itself reads, writes, and re-reads its own tdata transparently (no user-visible regression).
4. Setting a local passcode works normally in our fork.
5. No changes to MTP protocol, server-side state, or account identity. Only local disk format diverges.

**Non-goals:**
- Defending against an attacker with source access (GitHub repo is accessible; a motivated party can extract the constant and build a compatible version). Threat model is explicitly non-expert.
- Migrating existing test data — dev stage, no production employees, tdata re-initialization on upgrade is acceptable.
- Extending `-workdir` behavior, changing default data directory, or any other defense-in-depth. One-function change is the entire mitigation.
- Obfuscation of the embedded constant (chose approach A from the brainstorming, not A+B XOR).
- Touching `CreateLegacyLocalKey`, which never fires on fresh installs.

---

## 2. Approach

Extend the input to the SHA-512 hash consumed by PBKDF2 inside `CreateLocalKey`, adding a fork-specific 32-byte constant. Vanilla Telegram Desktop hashes `salt || passcode || salt`; this fork hashes `salt || passcode || salt || kEmployeeSalt`. Different hash → different PBKDF2 output → different `localKey` → vanilla cannot decrypt our tdata.

Everything downstream of `localKey` — account authKeys, encrypted messages, contacts, settings, cache — is transitively protected. No other storage-layer changes.

---

## 3. Components

### Single modification site

File: `Telegram/SourceFiles/storage/details/storage_file_utilities.cpp:301` — `CreateLocalKey(const QByteArray &passcode, const QByteArray &salt)`

### Change

Before:
```cpp
MTP::AuthKeyPtr CreateLocalKey(
        const QByteArray &passcode,
        const QByteArray &salt) {
    const auto s = bytes::make_span(salt);
    const auto hash = openssl::Sha512(s, bytes::make_span(passcode), s);
    const auto iterationsCount = passcode.isEmpty()
        ? 1 // Don't slow down for no password.
        : kStrongIterationsCount;

    auto key = MTP::AuthKey::Data{ { gsl::byte{} } };
    PKCS5_PBKDF2_HMAC(
        reinterpret_cast<const char*>(hash.data()),
        hash.size(),
        reinterpret_cast<const unsigned char*>(s.data()),
        s.size(),
        iterationsCount,
        EVP_sha512(),
        key.size(),
        reinterpret_cast<unsigned char*>(key.data()));
    return std::make_shared<MTP::AuthKey>(key);
}
```

After:
```cpp
namespace {
// Fork-specific secret mixed into the local-key derivation SHA-512
// pre-image. Vanilla Telegram Desktop computes
// SHA-512(salt || passcode || salt); this fork computes
// SHA-512(salt || passcode || salt || kEmployeeSalt). Different hash →
// different PBKDF2 output → different localKey. As a result, a vanilla
// build opening this fork's tdata derives the wrong localKey, fails to
// decrypt the encrypted passcode-key blob, and treats the data as
// corrupt. The constant itself is not a secret in the cryptographic
// sense; its value only needs to be a stable, fork-unique byte string.
constexpr std::array<std::byte, 32> kEmployeeSalt{
    /* 32 bytes of fixed pseudo-random values — final bytes chosen at
       implementation time. */
};
} // namespace

MTP::AuthKeyPtr CreateLocalKey(
        const QByteArray &passcode,
        const QByteArray &salt) {
    const auto s = bytes::make_span(salt);
    const auto hash = openssl::Sha512(
        s,
        bytes::make_span(passcode),
        s,
        bytes::make_span(kEmployeeSalt));
    const auto iterationsCount = passcode.isEmpty()
        ? 1 // Don't slow down for no password.
        : kStrongIterationsCount;

    auto key = MTP::AuthKey::Data{ { gsl::byte{} } };
    PKCS5_PBKDF2_HMAC(
        reinterpret_cast<const char*>(hash.data()),
        hash.size(),
        reinterpret_cast<const unsigned char*>(s.data()),
        s.size(),
        iterationsCount,
        EVP_sha512(),
        key.size(),
        reinterpret_cast<unsigned char*>(key.data()));
    return std::make_shared<MTP::AuthKey>(key);
}
```

**Two additions:**
1. An anonymous-namespace `kEmployeeSalt` constant of 32 bytes at the top of the file (or near the top of whichever namespace `CreateLocalKey` lives in).
2. A fourth argument to the `openssl::Sha512(...)` call — the bytes of `kEmployeeSalt`.

No other code change. Signature preserved. Callers unaffected.

### Why `openssl::Sha512` accepts four arguments

`Telegram/lib_base/base/openssl_help.h:588-597` defines a variadic overload:
```cpp
template <typename ...Args, typename = std::enable_if_t<(sizeof...(Args) > 1)>>
inline bytes::vector Sha512(Args &&...args) { ... }
```
so `Sha512(a, b, c, d)` compiles and streams all four spans into the same SHA-512 update. No helper library changes needed.

---

## 4. Data Flow

### Fresh install (this fork)

```
Domain::generateLocalKey()
  → CreateLocalKey(randomPass, randomSalt)
    → hash = SHA-512(randomSalt || randomPass || randomSalt || kEmployeeSalt)
    → PBKDF2(hash, randomSalt) → localKey K_fork
  → encryptLocalKey(passcode="")
    → _passcodeKey = CreateLocalKey("", newSalt)
      → hash = SHA-512(newSalt || "" || newSalt || kEmployeeSalt)
      → PBKDF2 → passcodeKey P_fork
    → encrypt(K_fork) with P_fork → passcodeKeyEncrypted_fork
  → write to tdata/<hash>/key_datas: [newSalt, passcodeKeyEncrypted_fork, infoEncrypted]
```

### Subsequent launch (this fork)

```
Domain::startModern()
  → read key_datas → newSalt, passcodeKeyEncrypted_fork
  → _passcodeKey = CreateLocalKey("", newSalt)
    → hash = SHA-512(newSalt || "" || newSalt || kEmployeeSalt)   ← same kEmployeeSalt
    → PBKDF2 → P_fork                                              ← same P_fork
  → DecryptLocal(passcodeKeyEncrypted_fork, P_fork) → K_fork       ← success
  → decrypt everything else with K_fork → success
```

### Vanilla opening this fork's tdata (attack)

```
Domain::startModern()
  → read key_datas → newSalt, passcodeKeyEncrypted_fork
  → _passcodeKey = CreateLocalKey("", newSalt)
    → hash = SHA-512(newSalt || "" || newSalt)                     ← no kEmployeeSalt
    → PBKDF2 → P_vanilla ≠ P_fork
  → DecryptLocal(passcodeKeyEncrypted_fork, P_vanilla)
    → decrypted bytes are random / fail integrity check
    → returns false
  → Domain::startModern returns StartModernResult::IncorrectPasscode (or Corrupted)
  → vanilla falls through to "no account" → prompts login-from-scratch
  → employee cannot access the previously-logged-in account from vanilla
```

### With a user-set passcode

```
Same as above, with passcode = user's password:
  fork:    hash = SHA-512(salt || userpass || salt || kEmployeeSalt) → P_fork_with_user_pass
  vanilla: hash = SHA-512(salt || userpass || salt)                   → P_vanilla_with_user_pass (≠ P_fork)
```
Same divergence. User-set passcode does not help vanilla decrypt.

---

## 5. Edge Cases

| Scenario | Behavior |
|---|---|
| First launch after this change on a dev install with pre-existing tdata | Pre-change tdata was encrypted without `kEmployeeSalt`. Post-change fork tries `CreateLocalKey` with `kEmployeeSalt` → derives a different key → cannot decrypt → fork treats as corrupt → employee re-logs in. Acceptable (dev stage, per user direction). |
| First launch on a completely clean install | Fork writes fresh tdata encrypted with `kEmployeeSalt`-aware derivation. Works normally. |
| User sets local passcode | `Domain::encryptLocalKey(passcode)` calls the same `CreateLocalKey` with the new passcode; same `kEmployeeSalt` mixed in; decryption on next unlock works. |
| User changes passcode | Uses same code path; same `kEmployeeSalt`; works. |
| Multiple accounts | `Storage::Domain` uses a single `localKey` across accounts; all derived keys inherit the `kEmployeeSalt` protection automatically. |
| Employee exports tdata to a backup, later imports back into the same fork | Works: decryption uses the same salt and produces the same key. |
| Employee copies tdata to an official Telegram Desktop | Vanilla cannot decrypt → sees corrupt data → behaves as fresh install. This is the success case. |
| Employee copies tdata to a different version of this fork built from the same source | Works (same `kEmployeeSalt` compiled in). Acceptable: same fork = same environment = expected to interoperate. |
| Existing legacy tdata (pre-modern format) opened by this fork | `CreateLegacyLocalKey` is unchanged; reads normally; migration then re-writes using the new `CreateLocalKey` → employee's legacy data transparently upgrades on first successful open. (No employee actually has such data at this dev stage, but behavior is correct.) |
| Vanilla writes new tdata in the default directory, employee later switches to this fork | Fork's `CreateLocalKey` cannot decrypt vanilla-written data → fork sees as corrupt → login-from-scratch. Symmetric to attack scenario. Acceptable: if a user is transitioning between forks, they re-log in. |

---

## 6. What Is Not Changed

- MTP protocol / MTP authKey content (the keys Telegram servers see are identical — this is local-disk-only hardening).
- Server-side state, employee authentication backend, employee token flow.
- `PeerData::kServiceNotificationsId` / `IsHiddenSystemUser` / UI guard helpers / SendButton hiding / username hiding / all previous fork customizations.
- `CreateLegacyLocalKey` (legacy-format reader; never called for fresh installs).
- `Storage::Domain`, `Storage::Account`, file formats, file paths, directory layout.
- Any caller of `CreateLocalKey` — only the function body changes; signature preserved.
- `openssl_help.h` — already provides the variadic overload.

---

## 7. Testing (Smoke)

Manual after implementation:

1. **Fork happy path**
   - Remove any existing `tdata/` (or use `-workdir` to a clean directory).
   - Launch this fork → log in with phone number → send a test message.
   - Quit → relaunch → app opens normally, session intact, messages visible.
   - Verifies encrypt+decrypt round-trip with `kEmployeeSalt`.

2. **Fork passcode path**
   - Same session. Open Settings → Privacy → Local Passcode → set a 4-digit code.
   - Quit → relaunch → passcode prompt appears → enter code → unlocks normally.
   - Verifies user-passcode flow still works.

3. **Vanilla bypass attempt (primary objective)**
   - Take the fork's `tdata/` directory (still in the work directory after step 1 or 2).
   - Copy it to vanilla Telegram Desktop's default data location (or launch vanilla with `-workdir` pointing at the fork's data path).
   - Launch vanilla.
   - Expected: vanilla either shows a "data corrupted" dialog / resets to the login screen / fails to auto-log-in. Either outcome proves the account from the fork is not accessible through vanilla.
   - Do NOT expect a graceful message referencing "employee mode" — the failure will look generic to vanilla.

4. **Regression: fork reads fork**
   - Completely clean state, launch fork, log in, quit, relaunch five times. Each time should open without re-login or passcode prompt (assuming no passcode set).

---

## 8. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| `kEmployeeSalt` value collides with a future vanilla constant | ~0 | 32 bytes random; collision probability negligible. |
| Public GitHub repo exposes `kEmployeeSalt` → motivated employee builds a custom vanilla-compatible variant | Out of threat model | Addressed by threat model scope ("non-expert employee"). If later concern, we can layer XOR obfuscation (brainstorming option B) or shift to API-id-derived secret (option C). |
| Vanilla reacts to decryption failure in an unexpected way (crash, infinite loop, tight retry loop) | Low | `tdesktop` already handles decryption failure via `StartModernResult::IncorrectPasscode` / `Corrupted`. Smoke 3 empirically confirms behavior. |
| Upstream refactors `CreateLocalKey` signature or derivation algorithm | Low-medium | Merge conflicts land on this one function; re-apply the 4th Sha512 argument + `kEmployeeSalt` constant; update in-place. |
| Someone rebuilds the fork with a different `kEmployeeSalt` value | Produces tdata incompatible with other fork builds — by design | Acceptable. If teams need interoperable builds, don't change the constant. |
| Debug / developer accidentally logs `kEmployeeSalt` contents | Low — nothing in the modified code path logs it | Mitigated by placing the constant in the local function scope / anonymous namespace. No LOG() added. |

---

## 9. Implementation Ordering

Single-task plan:
1. Grep `CreateLocalKey` / `Sha512` variadic overload to confirm state matches spec.
2. Generate 32 bytes of pseudo-random data for `kEmployeeSalt` (e.g., `openssl rand -hex 32` and render as `std::byte{...}` literals).
3. Insert the anonymous-namespace constant + extend the `Sha512(...)` call with the new argument.
4. Compile + run smoke tests 1-4.
5. Commit + tag (`tdata-fork-isolation-v1`) + push.

Total expected LOC change: ~10 lines (1 namespace block + 1 call-site addition).
