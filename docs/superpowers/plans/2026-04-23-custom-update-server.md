# Custom Update Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Redirect the built-in tdesktop auto-updater from Telegram's official servers to a GitHub-hosted manifest + Releases, and replace the embedded RSA public keys with ones this fork controls.

**Architecture:** Three small, independent edits across three files. No new files, no new includes, no `#ifdef` gating. Server-side packaging / signing pipeline is deliberately out of scope.

**Tech Stack:** C++, RSA-1024 PEM keys, tdesktop's existing updater logic unchanged.

**Spec:** `docs/superpowers/specs/2026-04-23-custom-update-server-design.md` (ce6877a)

**Branch baseline:** `customization` @ ce6877a.

---

## Global Conventions

### G1. No `#ifdef TDESKTOP_EMPLOYEE_MODE` wrap
Fork-wide default. Unconditional C++.

### G2. Single file per hunk, no cross-file refactor
Three distinct edits; each is a contained change in one file.

### G3. Workflow
Implementer writes code and self-reviews; user compiles on MSVC and smoke-tests. Controller commits after confirmation.

### G4. Commit style
```
feat(fork): redirect auto-updater to GitHub-hosted manifest

<body>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Task 1: Redirect updater URL + replace RSA keys + support absolute link

### Files
Modify:
- `Telegram/SourceFiles/storage/localstorage.cpp` (around line 556)
- `Telegram/SourceFiles/config.h` (lines 50-64)
- `Telegram/SourceFiles/core/update_checker.cpp` (around line 773)

### Step 1: Context analysis

- [ ] Confirm the three anchors still resolve where the spec says:

```bash
grep -n '"https://td.telegram.org"' Telegram/SourceFiles/storage/localstorage.cpp
grep -n "UpdatesPublicKey\|UpdatesPublicBetaKey" Telegram/SourceFiles/config.h
grep -n "Local::readAutoupdatePrefix() + bestLink" Telegram/SourceFiles/core/update_checker.cpp
```

Expected:
- `localstorage.cpp` ~line 556, `return AutoupdatePrefix("https://td.telegram.org");`
- `config.h` ~lines 50 and 58, one `static const char *UpdatesPublicKey = ...` and one `static const char *UpdatesPublicBetaKey = ...`
- `update_checker.cpp` ~line 773, `Local::readAutoupdatePrefix() + bestLink` inside `HttpChecker::parseResponse`

If any anchor is absent, STOP and report NEEDS_CONTEXT with what you found.

- [ ] Confirm the user's GitHub repo is reachable:

```bash
curl -sS -o /dev/null -w "%{http_code}\n" "https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main/current4"
```

Expected: `200`. The user has already placed an example `current4` file at that path. If you get `404`, surface that immediately — we don't want to ship a URL that returns 404.

### Step 2: Change the default update URL

File: `Telegram/SourceFiles/storage/localstorage.cpp`

Before (around line 556):
```cpp
	return AutoupdatePrefix("https://td.telegram.org");
```

After:
```cpp
	return AutoupdatePrefix("https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main");
```

Exactly one line changed. No other change in this file.

### Step 3: Replace both RSA public keys

File: `Telegram/SourceFiles/config.h`

**Before (around line 50-56):**
```cpp
static const char *UpdatesPublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBAMA4ViQrjkPZ9xj0lrer3r23JvxOnrtE8nI69XLGSr+sRERz9YnUptnU\n\
BZpkIfKaRcl6XzNJiN28cVwO1Ui5JSa814UAiDHzWUqCaXUiUEQ6NmNTneiGx2sQ\n\
+9PKKlb8mmr3BB9A45ZNwLT6G9AK3+qkZLHojeSA+m84/a6GP4svAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";
```

**After:**
```cpp
static const char *UpdatesPublicKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBAKsaums6nuUqpI+AGs6GPMM5S+On0oY1vF3knAL+fY3EuMOcwKFCB/DY\n\
2gLP/v6M4ngsHBG8YzzfNEKJ6brnYkTNLjsjgMSe0UNAeTyKUJHttPn68AQMd9+k\n\
AcT+pFFYbvnta9P9ZS2puBR9fDLIwFdbJRv4IYSPlSiRMrDxxY51AgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";
```

**Before (around line 58-64):**
```cpp
static const char *UpdatesPublicBetaKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBALWu9GGs0HED7KG7BM73CFZ6o0xufKBRQsdnq3lwA8nFQEvmdu+g/I1j\n\
0LQ+0IQO7GW4jAgzF/4+soPDb6uHQeNFrlVx1JS9DZGhhjZ5rf65yg11nTCIHZCG\n\
w/CVnbwQOw0g5GBwwFV3r0uTTvy44xx8XXxk+Qknu4eBCsmrAFNnAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";
```

**After:**
```cpp
static const char *UpdatesPublicBetaKey = "\
-----BEGIN RSA PUBLIC KEY-----\n\
MIGJAoGBAOaXY7X4E7XLy5tmc7fUpl7lSK/QU34BbYkgayEmZxCRQHD/QMVmr+g5\n\
nDLyv1FmdFV04/ObkF9Zufiun5TVB96GtUyMn95B4EyRtyPNTlj/63mz0n0DgDmF\n\
B3/VAL89Y1aEdee1bGJLEkz7leqA7zWhI83pxxKpS9myKF+PgGELAgMBAAE=\n\
-----END RSA PUBLIC KEY-----\
";
```

Only the inner 3 base64 lines of each key block change. The variable name, header / footer lines, trailing `\"\\n\\` escape semicolon structure, and surrounding code are identical.

### Step 4: Support absolute URLs in manifest `link`

File: `Telegram/SourceFiles/core/update_checker.cpp`

Before (around line 770-773, inside `HttpChecker::parseResponse`):
```cpp
	return validateLatestUrl(
		bestAvailableVersion,
		bestIsAvailableAlpha,
		Local::readAutoupdatePrefix() + bestLink);
```

After:
```cpp
	const auto isAbsolute = bestLink.startsWith(u"http://"_q)
		|| bestLink.startsWith(u"https://"_q);
	return validateLatestUrl(
		bestAvailableVersion,
		bestIsAvailableAlpha,
		isAbsolute ? bestLink : (Local::readAutoupdatePrefix() + bestLink));
```

Two behaviours:
- If `link` in the manifest JSON starts with `http://` or `https://` (absolute), use it verbatim.
- Otherwise, prepend `readAutoupdatePrefix()` (relative path, legacy behaviour).

Tab indent matches surrounding lines.

### Step 5: Self-review

- [ ] Three edits, three distinct files. No other files touched.
- [ ] Old RSA public-key base64 strings are completely removed; no fallback to Telegram's keys.
- [ ] New public keys match the ones in `/mnt/d/ProjectKaka/TelegramProClient/telgram发包密钥/stable_public.pem` and `beta_public.pem`.
- [ ] `isAbsolute` branch uses `u""_q` literals (matches existing `qstr` / `_q` style in the file).
- [ ] No `#ifdef` gate added.
- [ ] No new include or header dependency.
- [ ] Spec §3.1, §3.2, §3.3 all visibly addressed.

### Step 6: Chinese bullet summary + hand off for compile

Emit:

```
编译前总结（custom update server）：
- storage/localstorage.cpp:556 默认更新 URL 改为 https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main
- config.h:50-64 两把 RSA 公钥（UpdatesPublicKey / UpdatesPublicBetaKey）整块替换为 fork 自家密钥（已和 /mnt/d/ProjectKaka/TelegramProClient/telgram发包密钥/ 私钥配对）
- core/update_checker.cpp:773 parseResponse 里对 manifest link 字段加 isAbsolute 判断：absolute URL 不拼接 prefix
- 无新文件、无新 include、无 #ifdef
请编译，然后 smoke：
  1. 启动 → Settings → Advanced → Check for updates
  2. 抓包或 GitHub raw 的 access log：应看到请求 https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/refs/heads/main/current4
  3. 客户端行为：解析 manifest 成功；若 manifest 里 released 小于当前版本号 → "已是最新"；若 released 更高但二进制未签名 → 签名校验失败（预期，因为还没打包）
```

Wait for "编译通过" + smoke confirmation.

### Step 7: Commit

After user confirms:

```bash
git add Telegram/SourceFiles/storage/localstorage.cpp \
        Telegram/SourceFiles/config.h \
        Telegram/SourceFiles/core/update_checker.cpp
git commit -m "$(cat <<'EOF'
feat(fork): redirect auto-updater to GitHub-hosted manifest

Three client-side changes to redirect the built-in tdesktop updater
from https://td.telegram.org to the fork's GitHub-hosted manifest
and Releases:

1. storage/localstorage.cpp: change default autoupdate prefix to
   the fork's raw.githubusercontent.com path.
2. config.h: replace both RSA-1024 public keys
   (UpdatesPublicKey and UpdatesPublicBetaKey) with the fork's own
   keys. Telegram's keys are fully removed (no fallback).
3. core/update_checker.cpp: teach HttpChecker::parseResponse to
   accept absolute URLs in manifest 'link' fields so binaries can
   be hosted at github.com/.../releases/download/... without sharing
   the raw-content prefix.

Private keys live off-repo at
/mnt/d/ProjectKaka/TelegramProClient/telgram发包密钥/. Server-side
packaging and signing pipeline (adapting _other/packer, generating
release blobs, updating the manifest) is deliberately out of scope.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Step 8: Tag + push

After the commit lands and user confirms:

```bash
git tag -a custom-update-server-v1 -m "$(cat <<'EOF'
Redirect auto-updater to GitHub-hosted manifest + Releases

Default update URL now points at
https://raw.githubusercontent.com/kakala666/TelegramProClientUpdate/
refs/heads/main, with RSA public keys replaced by the fork's own
pair (private keys held off-repo). Manifest 'link' field supports
absolute URLs so GitHub Releases hosts the signed binaries.

Server-side packaging / signing pipeline is a separate follow-up.
EOF
)"

git push origin customization
git push origin custom-update-server-v1
```

Require explicit user confirmation before pushing.

---

## Spec Coverage Self-Check

| Spec section | Covered by |
|---|---|
| §1 Success criterion 1 (fresh install hits raw.githubusercontent URL) | Step 2 |
| §1 Success criterion 2 (parses JSON manifest) | Unchanged upstream logic; no client-side change needed for parsing |
| §1 Success criterion 3 (downloads + verifies signed binary from Release) | Step 3 (keys) + Step 4 (absolute URL); actual binary availability is server-side, out of scope |
| §1 Success criterion 4 (manifest link supports absolute URLs) | Step 4 |
| §1 Success criterion 5 (Telegram keys fully replaced, no fallback) | Step 3 replaces both key strings |
| §1 Success criterion 6 (no MTP / server / directory / tdata change) | Step 1 constraint: three files, three small hunks, nothing else |
| §3.1 URL change | Step 2 |
| §3.2 RSA key replacement | Step 3 |
| §3.3 Absolute-URL support | Step 4 |
| §5 What is not changed | Step 1 scope constraint |
| §6 Edge cases (stale tdata override, invalid manifest, missing binary, etc.) | No client-side logic needed — behaviours inherent to unchanged parsing + verification code |
| §7 Testing (smoke matrix) | Step 6 Chinese summary enumerates the smoke steps |
| §8 Risks | Minimal footprint (3 hunks, no new abstractions) keeps risk surface small |
| §10 Out-of-scope follow-up (packer, packaging pipeline) | Not in plan by design; referenced in commit body |
