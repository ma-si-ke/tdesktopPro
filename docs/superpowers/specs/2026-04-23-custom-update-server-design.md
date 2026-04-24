# Custom Update Server Design Spec

**Date:** 2026-04-23
**Status:** Approved
**Depends on:** none
**Scope:** client-side redirect only. Server-side packaging / signing pipeline is out of scope (tracked separately).

---

## 1. Goal

Redirect the tdesktop auto-updater away from Telegram's official update servers (`https://td.telegram.org`) to a self-hosted manifest on GitHub. Replace the embedded RSA public keys so only binaries we sign (with the matching private keys stored locally) will pass verification. Do NOT rewrite any updater logic.

**Success criteria:**
1. Fresh install of this fork checks for updates at `https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main/current4` instead of `https://td.telegram.org/current4`.
2. The updater parses a JSON manifest from that URL (format documented in §4).
3. If the manifest advertises a new version, the updater downloads and verifies a signed binary from a GitHub Release; only binaries signed with our private key pass verification.
4. Manifest `link` field supports absolute URLs (so Release assets work without sharing the manifest's raw-content prefix).
5. Telegram's original RSA public keys are fully replaced. Binaries signed by Telegram cannot pass our verification. No fallback.
6. No change to MTP / server / directory / tdata format. No change to `Ui`.

**Non-goals:**
- Packaging, signing, or releasing binaries — out of scope, handled by a future server-side task using the keys in `/mnt/d/ProjectKaka/TelegramProClient/telgram发包密钥/`.
- Updating the `_other/packer` tool. That's part of the server-side work later.
- Supporting both old and new signing keys simultaneously. All-or-nothing replace.
- Adding a settings UI to let users pick a different update URL. Fork hardcodes default; if an employee has a stale override in their tdata it will be superseded at next fresh init (and since `tdata-fork-isolation-v1` already resets tdata for all current testers, there are no stale overrides in practice).

---

## 2. Baseline findings (reverse-engineered)

### 2.1 Where the updater lives
- Entry points: `Core::UpdateChecker().start()` called from `settings_advanced.cpp:1027`, `mainwidget.cpp:431`, and app startup.
- HTTP path builder: `HttpChecker::start()` at `core/update_checker.cpp:655`:
  ```cpp
  const auto path = Local::readAutoupdatePrefix()
      + qstr("/current")
      + (updaterVersion > 1 ? QString::number(updaterVersion) : QString());
  ```
- Response parser: `HttpChecker::parseResponse()` at `core/update_checker.cpp:741`.
- Binary loader: `HttpLoader` (triggered from `HttpChecker::handleResponse` on line 691).

### 2.2 Where the default URL is stored
- `Local::readAutoupdatePrefix()` (runtime getter) in `storage/localstorage.cpp:556` returns `"https://td.telegram.org"` when no override is set in persistent settings.

### 2.3 Where RSA keys are stored
- Two hardcoded PEM strings in `Telegram/SourceFiles/config.h:50-64`:
  - `UpdatesPublicKey` — stable channel
  - `UpdatesPublicBetaKey` — beta channel
- Both are PKCS#1 RSA-1024 public keys (`-----BEGIN RSA PUBLIC KEY-----` header).

### 2.4 How `link` is resolved into a full download URL
- `parseResponse` extracts the `link` string from the JSON manifest.
- At `update_checker.cpp:773`: `Local::readAutoupdatePrefix() + bestLink` — simple concatenation. No handling for absolute URLs. Currently relies on `link` being a relative path like `/tsetup.{version}.exe`.

### 2.5 How binary signatures are verified
- Header layout of the package (`update_checker.cpp:272-346` in `UnpackUpdate`):
  - Bytes 0-127: RSA-1024 signature (128 bytes)
  - Bytes 128-147: SHA1 hash of the rest (20 bytes)
  - Bytes 148-152: LZMA properties (5 bytes on Windows only; 0 bytes on Linux)
  - Next 4 bytes: uncompressed size (uint32 little-endian)
  - Remainder: LZMA-compressed Qt QDataStream payload
- Verification: `RSA_verify(NID_sha1, sha1, 20, sig, 128, rsa_key)`.
- Client tries both keys (stable + beta) if the first fails.

---

## 3. Client-side changes (in-scope)

Exactly three modifications to the client. No new files.

### 3.1 Change default update URL

File: `Telegram/SourceFiles/storage/localstorage.cpp` (around line 556)

**Before:**
```cpp
return u"https://td.telegram.org"_q;
```

**After:**
```cpp
return u"https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main"_q;
```

After change, `HttpChecker::start()` builds request URLs like:
- `https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main/current`
- `https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main/current2`
- `https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main/current4`

(Which of `current`/`current2-4` is used depends on platform; `current4` covers the modern win64/linux/winarm targets.)

### 3.2 Replace RSA public keys

File: `Telegram/SourceFiles/config.h` (lines 50-64)

**Before (Telegram's stable key):**
```cpp
static const char *UpdatesPublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBAMA4ViQrjkPZ9xj0lrer3r23JvxOnrtE8nI69XLGSr+sRERz9YnUptnU\n\
BZpkIfKaRcl6XzNJiN28cVwO1Ui5JSa814UAiDHzWUqCaXUiUEQ6NmNTneiGx2sQ\n\
+9PKKlb8mmr3BB9A45ZNwLT6G9AK3+qkZLHojeSA+m84/a6GP4svAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";
```

**After (fork's stable key, corresponds to `stable_private.pem`):**
```cpp
static const char *UpdatesPublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBAKsaums6nuUqpI+AGs6GPMM5S+On0oY1vF3knAL+fY3EuMOcwKFCB/DY\n\
2gLP/v6M4ngsHBG8YzzfNEKJ6brnYkTNLjsjgMSe0UNAeTyKUJHttPn68AQMd9+k\n\
AcT+pFFYbvnta9P9ZS2puBR9fDLIwFdbJRv4IYSPlSiRMrDxxY51AgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";
```

Same treatment for `UpdatesPublicBetaKey` — replace with the beta public key.

**Fork's beta key (corresponds to `beta_private.pem`):**
```cpp
static const char *UpdatesPublicBetaKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBAOaXY7X4E7XLy5tmc7fUpl7lSK/QU34BbYkgayEmZxCRQHD/QMVmr+g5\n\
nDLyv1FmdFV04/ObkF9Zufiun5TVB96GtUyMn95B4EyRtyPNTlj/63mz0n0DgDmF\n\
B3/VAL89Y1aEdee1bGJLEkz7leqA7zWhI83pxxKpS9myKF+PgGELAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";
```

### 3.3 Support absolute URLs in manifest `link`

File: `Telegram/SourceFiles/core/update_checker.cpp` (around line 773, inside `HttpChecker::parseResponse`)

**Before:**
```cpp
return validateLatestUrl(
    bestAvailableVersion,
    bestIsAvailableAlpha,
    Local::readAutoupdatePrefix() + bestLink);
```

**After:**
```cpp
const auto isAbsolute = bestLink.startsWith(u"http://"_q)
    || bestLink.startsWith(u"https://"_q);
return validateLatestUrl(
    bestAvailableVersion,
    bestIsAvailableAlpha,
    isAbsolute ? bestLink : (Local::readAutoupdatePrefix() + bestLink));
```

This enables the manifest to embed a full `https://github.com/.../releases/download/...` URL for the binary without prepending the raw-content prefix.

---

## 4. Server-side contract (documentation for future server work)

This section is a reference for building the self-hosted update server. It is not implemented as part of this task.

### 4.1 Manifest endpoint

**URL pattern:**
```
GET https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main/current[N]
```
where `[N]` is `` (empty), `2`, `3`, or `4` depending on client platform. The simplest deployment serves an identical JSON at all four paths (or at least `current4`, which modern Win64 / Linux / Win ARM64 clients request).

### 4.2 Manifest JSON format

Root is an object keyed by platform code. Each platform is an object keyed by channel. Each channel has `released` (the target version) and `link` (where to download the binary).

```json
{
  "win64": {
    "stable": {
      "released": 5000000,
      "link": "https://github.com/kakala666/TelegramProClientUpdate/releases/download/v{version}/tsetup.{version}.exe"
    },
    "beta": {
      "released": 5001000,
      "link": "https://github.com/kakala666/TelegramProClientUpdate/releases/download/v{version}-beta/tsetup.{version}.exe"
    }
  },
  "winarm": {
    "stable": {
      "released": 5000000,
      "link": "https://github.com/kakala666/TelegramProClientUpdate/releases/download/v{version}/tsetup-arm64.{version}.exe"
    }
  },
  "linux": {
    "stable": {
      "released": 5000000,
      "link": "https://github.com/kakala666/TelegramProClientUpdate/releases/download/v{version}/tlinuxupd.{version}.tar.xz"
    }
  }
}
```

### 4.3 Field reference

| Field | Type | Meaning |
|---|---|---|
| top-level key | string | Platform code: `win64`, `win`, `winarm`, `linux`, `mac`. Must match `Platform::AutoUpdateKey()` in the client. |
| channel key | string | `stable`, `beta`, or `alpha`. |
| `released` | string or number | Target version. Client parses either. Use the tdesktop version-to-uint64 convention (e.g., `5.0.0` → `5000000`). Client compares to its own `AppVersion`; if `released <= AppVersion`, no update offered. |
| `link` | string | Absolute `https://...` URL to the signed binary. `{version}` (and `{signature}` for alpha) are placeholders replaced by the client. |

### 4.4 Binary package format

The downloaded binary must be a specifically-structured blob:

```
Offset  Size    Content
0       128     RSA-1024 signature (big-endian bytes)
128     20      SHA1 hash of everything from offset 148 onward
148     5       LZMA properties (Windows only; zero bytes on Linux)
153/148 4       Uncompressed payload size (uint32 little-endian)
157/152 ...     LZMA-compressed Qt QDataStream payload
```

Signature = `RSA-SHA1(SHA1(bytes[148:end]))` using `stable_private.pem` (for stable channel) or `beta_private.pem` (for beta channel).

Packing workflow: reuse tdesktop's `_other/packer` tool (to be adapted in a later task to sign with our private keys instead of Telegram's).

---

## 5. What is not changed

- `CreateLocalKey` / tdata encryption layer — orthogonal to updater.
- Update protocol, HTTP flow, QDataStream payload format, LZMA decompression, signature verification algorithm. The changes in §3 are config-level (URL, keys) plus one small behavioral tweak (absolute URL pass-through).
- The `_other/packer` tool (server-side, deferred).
- Existing tdesktop customization: UI disable, hide-service-notifications, hide-user-username, tdata-fork-isolation, all employee-permission features.
- The "Check for updates" button itself — still visible, still functional. Only what it checks against changes.

---

## 6. Edge Cases

| Scenario | Behavior |
|---|---|
| Employee's tdata contains an overridden `autoupdatePrefix` from before this change | Since `tdata-fork-isolation-v1` already invalidated all dev tdata, every employee tdata will be re-initialized → `readAutoupdatePrefix()` returns the new default. No migration needed. |
| Manifest is invalid JSON | `HttpChecker::parseResponse` returns `nullopt` → `parseOldResponse` is tried (legacy text format) → if that also fails, `gotFailure` fires. Client logs, user sees no update available. |
| Manifest advertises `released` ≤ current `AppVersion` | `validateLatestUrl` returns empty string → client treats as "up to date." |
| Manifest advertises a newer version but the `link` URL 404s or the Release hasn't been created | `HttpLoader` fails → client logs, no update installed. |
| `link` is absolute but points outside github.com / raw.githubusercontent.com | Works — the `isAbsolute` check only strips prefix-prepending; no domain whitelisting. Acceptable: we control the manifest, so we control `link`. |
| Binary signature is invalid | `UnpackUpdate` returns failure → update is discarded. Client may retry or give up. User not misled. |
| An official Telegram-signed binary somehow served from our URL | Signature verifies against Telegram's key → but we removed that key → verification fails → discarded. Official binaries can't be installed through our pipeline. |
| GitHub raw is rate-limited / temporarily unreachable | Request fails → client shows "update check failed" → normal retry on next trigger (periodic or manual). |

---

## 7. Testing (smoke)

Manual after implementation:

1. **Fresh install** — clear any existing tdata or use a fresh `-workdir`. Launch the client. Settings → Advanced → tap "Check for updates". Observe network activity via Wireshark/proxy/browser devtools or `curl` the client's process to confirm the request goes to `https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main/current4`.

2. **Successful manifest parse** — the client reads the JSON committed at that URL (which the user has already placed). Check client logs for "Update Info:" lines. Expected: no crash, no parse error.

3. **No update available (lower version)** — if manifest's `released` ≤ current `AppVersion`, client should log "already up to date" equivalent and show no update prompt.

4. **Update available but binary missing/unsigned (partial future readiness)** — if manifest's `released` > current version but the linked Release asset doesn't exist yet OR isn't signed with our key, client should log a download / signature failure and NOT install anything. Prove the new signature path fails gracefully.

5. **Absolute URL path works** — the `link` field in the user's manifest is already absolute (`https://github.com/...`). Verify client doesn't double-prepend the raw URL prefix.

6. **Regression: existing features intact** — launch, send a message, open any chat, pick a recipient in a forward flow. All prior customizations still work.

---

## 8. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Key file leaked from the user's machine | Depends on local security | User must store `stable_private.pem` and `beta_private.pem` somewhere only they access. Never commit. |
| GitHub rate-limits / disables account / changes raw.githubusercontent.com URL structure | Low-medium | If URL structure changes, re-deploy a new fork build with updated default URL. Operations concern, not code. |
| `core/update_checker.cpp` refactored upstream | Low | Three small edit sites; conflicts trivial to hand-resolve. |
| `{version}` template replacement produces a URL that doesn't exist at GitHub | Operational | Manifest author (user / release scripts) must keep `released` version in sync with Release tag. |
| Employee runs client before manifest points at a valid signed binary | Low | Update check fails gracefully (tested in smoke 4). Client keeps running old (current) version. |
| Telegram-signed binaries somehow leak through | None | Keys fully replaced; no fallback. Any Telegram-signed blob fails verification. |

---

## 9. Implementation Ordering

Single-task plan. One task with 3 small file edits:
1. Confirm anchors via grep (URL prefix line, RSA key lines, parseResponse block).
2. Edit `storage/localstorage.cpp` line 556 — new URL string.
3. Edit `config.h` lines 50-64 — replace both RSA public keys.
4. Edit `core/update_checker.cpp` around line 773 — absolute URL pass-through.
5. Chinese summary, compile handoff, user smoke test.
6. Commit + tag (`custom-update-server-v1`) + push.

Total LOC change: ~25 lines (1 URL + 2 × 5-line PEMs + 3-line conditional).

---

## 10. Out-of-scope follow-up work (future)

This spec is deliberately scoped to the client only. To actually ship updates, the user will need, in a later project:

1. **Adapt `Telegram/SourceFiles/_other/packer`** — the tdesktop packaging tool — to sign with our private keys (`stable_private.pem` or `beta_private.pem`) instead of Telegram's.
2. **Packaging pipeline** — a script that takes a freshly-built Win64 / Linux / WinArm binary, wraps it in the LZMA+signature envelope, and uploads it to a GitHub Release.
3. **Release cadence** — process for incrementing `released` version in the manifest + tagging a Release + uploading assets.
4. **Beta channel rollout** — if/when needed, serve a `beta` block in the manifest pointing at a separate set of binaries signed with the beta private key.

None of these are client-side concerns and do not block the client-side change.
