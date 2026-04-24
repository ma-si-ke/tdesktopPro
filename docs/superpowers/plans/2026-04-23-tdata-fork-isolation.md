# tdata Fork Isolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Mix a 32-byte fork-specific constant into tdata's `CreateLocalKey` SHA-512 pre-image so that vanilla Telegram Desktop cannot decrypt this fork's local storage.

**Architecture:** Single function change in `storage/details/storage_file_utilities.cpp`. Add a `constexpr` 32-byte array to the existing anonymous namespace; extend the variadic `openssl::Sha512(...)` call inside `CreateLocalKey` with a 4th argument referencing that array. No new files, no new includes, no `#ifdef` gating.

**Tech Stack:** C++ constexpr array, OpenSSL SHA-512 via tdesktop's variadic helper, PBKDF2 unchanged.

**Spec:** `docs/superpowers/specs/2026-04-23-tdata-fork-isolation-design.md` (b9f0fc2)

**Branch baseline:** `customization` @ b9f0fc2.

---

## Global Conventions

### G1. No `#ifdef TDESKTOP_EMPLOYEE_MODE` wrap
This is the entire fork's default behavior. Plain unconditional C++.

### G2. Single file, single function
Do not touch anything else.

### G3. Workflow
The implementer subagent writes code and self-reviews but does NOT compile. The user compiles on MSVC and smoke-tests; after confirmation, the controller commits.

### G4. Commit style
```
feat(fork): encrypt tdata with fork-specific salt

<body>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Task 1: Inject fork salt into CreateLocalKey

### File
Modify: `Telegram/SourceFiles/storage/details/storage_file_utilities.cpp` (single file, two small hunks)

### Step 1: Context analysis

- [ ] Confirm anchors and structure:

```bash
grep -n "^namespace {\|^}\s*//\s*namespace\|constexpr auto kStrongIterationsCount\|^MTP::AuthKeyPtr CreateLocalKey\|openssl::Sha512(s," Telegram/SourceFiles/storage/details/storage_file_utilities.cpp
```

Expected:
- `namespace {` at line ~21 (opens the anonymous namespace inside `Storage::details::{}`)
- `constexpr auto kStrongIterationsCount = 100'000;` at line ~26 (good neighbor for the new constant)
- `} // namespace` at line ~239 (closes the anonymous namespace)
- `MTP::AuthKeyPtr CreateLocalKey(` at line ~301
- `openssl::Sha512(s, bytes::make_span(passcode), s);` around line ~305 (target call)

- [ ] Confirm `openssl::Sha512` accepts 4 span arguments:

```bash
grep -A4 'typename ...Args' Telegram/lib_base/base/openssl_help.h | head -12
```

Expected: the variadic `Sha512(Args &&...args)` overload at line ~588 with `SFINAE enable_if_t<(sizeof...(Args) > 1)>` — already accepts 2, 3, 4, or more arguments.

- [ ] Confirm `bytes::make_span` accepts a plain C array:

```bash
grep -n "make_span(kPublicKey\|make_span(kBuiltIn" Telegram/SourceFiles/ -r --include="*.cpp" | head -3
```

Expected: multiple existing call sites that pass raw byte / char arrays to `bytes::make_span`. Confirms the pattern we'll use.

### Step 2: Add `kEmployeeSalt` constant to the anonymous namespace

In `Telegram/SourceFiles/storage/details/storage_file_utilities.cpp`, locate the line:

```cpp
constexpr auto kStrongIterationsCount = 100'000;
```

Immediately after this line (and before the next declaration `struct WriteEntry {`), insert:

```cpp

// Fork-specific secret mixed into the local-key derivation SHA-512
// pre-image. Vanilla Telegram Desktop computes
// SHA-512(salt || passcode || salt); this fork computes
// SHA-512(salt || passcode || salt || kEmployeeSalt). Different hash →
// different PBKDF2 output → different localKey. As a result, a vanilla
// build opening this fork's tdata derives the wrong localKey, fails to
// decrypt the encrypted passcode-key blob, and treats the data as
// corrupt. The constant's value is not a cryptographic secret; it only
// needs to be stable and distinct from vanilla's empty suffix.
constexpr unsigned char kEmployeeSalt[] = {
	0xf4, 0x7e, 0x0b, 0x8f, 0xef, 0xae, 0x12, 0x95,
	0xc8, 0x17, 0xc3, 0x59, 0xd6, 0x53, 0xb6, 0x38,
	0x99, 0x4c, 0x49, 0x75, 0xd0, 0x6c, 0xef, 0xf9,
	0x8f, 0x0c, 0x48, 0xa4, 0x65, 0x83, 0xbf, 0xe2,
};
```

Use tab indentation (match surrounding code).

### Step 3: Extend the `openssl::Sha512(...)` call inside `CreateLocalKey`

In the same file, locate the function body at ~line 301. Current:

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

Change only ONE line (the `openssl::Sha512` call) to include `kEmployeeSalt` as a fourth argument:

```cpp
	const auto hash = openssl::Sha512(
		s,
		bytes::make_span(passcode),
		s,
		bytes::make_span(kEmployeeSalt));
```

(The original single-line form can stay single-line if preferred, but breaking across 4 lines is cleaner for diff readability and matches upstream style for multi-arg calls.)

No other change to this function. Do NOT touch `CreateLegacyLocalKey` at line ~323.

### Step 4: Self-review

- [ ] Exactly one constant added (`kEmployeeSalt`) in the anonymous namespace; located next to `kStrongIterationsCount` for visual adjacency with other PBKDF2-related constants.
- [ ] Exactly one call site changed — the `openssl::Sha512(...)` inside `CreateLocalKey`.
- [ ] The 32-byte array has exactly 32 byte values — count them.
- [ ] Indentation uses tabs, matches surrounding code.
- [ ] No new `#include` added.
- [ ] No `#ifdef` wrapping.
- [ ] `CreateLegacyLocalKey` at line ~323 untouched.
- [ ] `bytes::make_span(kEmployeeSalt)` compiles because `bytes::make_span` (see `base/bytes.h`) accepts raw arrays via gsl::make_span decay.

### Step 5: Chinese bullet summary + hand off for compile

Emit:

```
编译前总结（tdata 隔离）：
- storage_file_utilities.cpp 匿名命名空间加 constexpr unsigned char kEmployeeSalt[32]（32 字节固定随机值）
- CreateLocalKey 里 openssl::Sha512(s, passcode, s) 改为 openssl::Sha512(s, passcode, s, kEmployeeSalt)
- CreateLegacyLocalKey 不动
- 无新文件、无新 include、无 #ifdef
请编译并 smoke：
  1. 清空或指定一个新 workdir → 启动 → 登录 → 退出 → 重启 → 应自动进入（无需重登）
  2. 设 passcode → 退出 → 重启 → 输入 passcode → 正常解锁
  3. 把定制版生成的 tdata 复制到官方版工作目录（或用 -workdir 指向同路径）→ 启动官方版 → 应报数据损坏或回到登录页（主目标验证）
```

Wait for "编译通过" + all 3 smoke items confirmed.

### Step 6: Commit

After all smoke items confirmed:

```bash
git add Telegram/SourceFiles/storage/details/storage_file_utilities.cpp
git commit -m "$(cat <<'EOF'
feat(fork): encrypt tdata with fork-specific salt

Add a 32-byte kEmployeeSalt constant to the anonymous namespace of
storage/details/storage_file_utilities.cpp and mix it into the
SHA-512 pre-image inside CreateLocalKey. Vanilla Telegram Desktop
hashes salt||passcode||salt; this fork hashes
salt||passcode||salt||kEmployeeSalt. Different hash → different
PBKDF2 output → different localKey. Vanilla cannot decrypt this
fork's tdata, preventing employees from bypassing customizations
by copying tdata into an official install.

No MTP / server / directory changes. Legacy-format reader
(CreateLegacyLocalKey) unchanged.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Step 7: Tag + push

After the commit lands and user explicitly confirms:

```bash
git tag -a tdata-fork-isolation-v1 -m "$(cat <<'EOF'
tdata fork isolation via CreateLocalKey salt extension

Encrypt local Telegram Desktop data with a fork-specific 32-byte
constant mixed into the SHA-512 pre-image used to derive localKey.
Vanilla Telegram Desktop cannot decrypt this fork's tdata.

Prevents employees from bypassing UI disable / notification
suppression / hide features / employee permission system by
copying tdata to an official Telegram Desktop install.
EOF
)"

git push origin customization
git push origin tdata-fork-isolation-v1
```

Require explicit user confirmation before pushing.

---

## Spec Coverage Self-Check

| Spec section | Covered by |
|---|---|
| §1 Success criterion 1 (fork writes tdata only fork can decrypt) | Step 2 + Step 3 |
| §1 Success criterion 2 (vanilla cannot decrypt → falls back to login) | Validated in smoke item 3 |
| §1 Success criterion 3 (fork reads its own tdata transparently) | Validated in smoke item 1 |
| §1 Success criterion 4 (passcode flow works) | Validated in smoke item 2 |
| §1 Success criterion 5 (no MTP / server-side change) | Step 3 constraint: only `CreateLocalKey` body; no touching of any MTP or server code |
| §2 Approach (SHA-512 pre-image extension) | Step 3 |
| §3.2 Single modification site | Step 3 at line ~301 |
| §3.3 Change content (constant + Sha512 arg) | Step 2 + Step 3 |
| §3.4 `openssl::Sha512` variadic support | Step 1 grep confirms overload at `openssl_help.h:588` |
| §5 Edge cases (pre-existing tdata, clean install, passcode, multiple accounts, backup/restore, attack scenario, legacy) | Smoke 1 + 2 + 3 + Step 3 constraint "CreateLegacyLocalKey untouched" |
| §6 What is not changed (MTP, server, all previous customizations, legacy reader) | Step 3 constraint |
| §7 Testing (smoke items 1-4) | Step 5 smoke list covers items 1-3; item 4 (fork-reads-fork regression) equivalent to smoke 1 |
| §8 Risks (collision / upstream refactor / etc.) | Minimal footprint (1 function, 1 constant) limits risk surface; acknowledged in spec |
