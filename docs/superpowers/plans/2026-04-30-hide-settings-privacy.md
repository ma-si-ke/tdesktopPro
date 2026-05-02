# Hide Privacy & Security Row in Main Settings Menu Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hide the "Privacy and Security" entry in the main Settings menu (the row between "Notifications" and "Chat Settings"), so employees cannot navigate to the Privacy section by clicking through Settings.

**Architecture:** Single-file change in `settings_main.cpp` — add a file-scope `constexpr auto kShowSettingsPrivacy = false;` next to the existing `kSugValidatePhone`, and wrap the 6-line `builder.addSectionButton({ .title = tr::lng_settings_section_privacy(), ... })` block with `if constexpr (kShowSettingsPrivacy) { ... }`. UI-only — deep-link routes, programmatic `showSettings(PrivacySecurityId())` calls, sub-page parentId registrations, and MTProto privacy APIs are all untouched.

**Tech Stack:** C++17 `if constexpr`.

**Spec:** `docs/superpowers/specs/2026-04-30-hide-settings-privacy-design.md` (7dee61c)

**Branch baseline:** `customization` @ 7dee61c.

---

## Global Conventions

### G1. No `#ifdef TDESKTOP_EMPLOYEE_MODE` wrapping
Fork-wide default. Plain C++.

### G2. No new files, no new includes
Single-file edit, two hunks (constant insertion + block wrap).

### G3. Chinese bullet summary before compile handoff
Emit a Chinese bullet summary of what changed, then stop and hand to the user for compile + smoke.

### G4. Workflow
The implementer subagent writes code and self-reviews but does NOT compile or commit. The user compiles on MSVC and smoke-tests; after confirmation, the controller commits and tags.

### G5. Mtime touch on edited files
After every Edit, run `touch <path>` on the file. WSL on `/mnt/d` (NTFS via 9P) sometimes skips mtime updates and MSBuild then thinks nothing changed.

### G6. Commit message style
```
feat(hide): hide Privacy & Security row in main Settings menu

<body>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Task 1: Add constexpr + wrap Privacy section button

### File
Modify: `Telegram/SourceFiles/settings/sections/settings_main.cpp` (single file, 2 hunks)

### Step 1: Context confirmation

- [ ] Verify anchor lines on baseline `7dee61c`:

```bash
grep -n "kSugValidatePhone\|^namespace {\|tr::lng_settings_section_privacy" Telegram/SourceFiles/settings/sections/settings_main.cpp | head -6
```

Expected (against current `customization` HEAD 7dee61c):
- `namespace {` at line 91
- `constexpr auto kSugValidatePhone = "VALIDATE_PHONE_NUMBER"_cs;` at line 95
- `.title = tr::lng_settings_section_privacy(),` at line 385 (inside the block at 384-389)

- [ ] Read lines 383-390 to confirm exact block boundaries:

```bash
sed -n '383,390p' Telegram/SourceFiles/settings/sections/settings_main.cpp
```

Expected output (preserved tab indentation, 1 leading tab for the function-body scope):

```cpp

	builder.addSectionButton({
		.title = tr::lng_settings_section_privacy(),
		.targetSection = PrivacySecurityId(),
		.icon = { &st::menuIconLock },
		.keywords = { u"security"_q, u"passcode"_q, u"password"_q, u"2fa"_q },
	});

```

The block opens with `builder.addSectionButton({` at line 384 and closes with `});` at line 389. The lines immediately above (377-382) are the "Notifications" section button block, and the lines immediately below (391-396) are the "Chat Settings" section button block — both stay outside the wrap.

### Step 2: Add file-scope constant

- [ ] Open `Telegram/SourceFiles/settings/sections/settings_main.cpp`. Current anonymous-namespace top (lines 91-95):

```cpp
namespace {

using namespace Builder;

constexpr auto kSugValidatePhone = "VALIDATE_PHONE_NUMBER"_cs;
```

- [ ] Insert one comment block + constant immediately after `kSugValidatePhone`. Final shape of lines 91-103:

```cpp
namespace {

using namespace Builder;

constexpr auto kSugValidatePhone = "VALIDATE_PHONE_NUMBER"_cs;

// Hide "Privacy and Security" row in main Settings menu. Force-hides
// the entry; deep-link routes (tg://settings/privacy_and_security) and
// programmatic showSettings(PrivacySecurityId()) calls (e.g., the
// suggest-archive-and-mute popup) still work — by design, this is UI-only.
// Sub-pages (Active Sessions, Blocked Peers, etc.) still register their
// parentId = PrivacySecurityId() — that's a lookup ID, unaffected.
constexpr auto kShowSettingsPrivacy = false;
```

The blank line and the existing class declarations that follow stay unchanged.

### Step 3: Wrap the Privacy `addSectionButton` block

- [ ] Locate the block at lines 384-389. Current code (preserved tab indentation, 1 leading tab):

```cpp
	builder.addSectionButton({
		.title = tr::lng_settings_section_privacy(),
		.targetSection = PrivacySecurityId(),
		.icon = { &st::menuIconLock },
		.keywords = { u"security"_q, u"passcode"_q, u"password"_q, u"2fa"_q },
	});
```

- [ ] Wrap it with `if constexpr (kShowSettingsPrivacy) { ... }`. Final form (every wrapped line gets one additional tab):

```cpp
	if constexpr (kShowSettingsPrivacy) {
		builder.addSectionButton({
			.title = tr::lng_settings_section_privacy(),
			.targetSection = PrivacySecurityId(),
			.icon = { &st::menuIconLock },
			.keywords = { u"security"_q, u"passcode"_q, u"password"_q, u"2fa"_q },
		});
	}
```

**Key constraints:**
- Only change: add `if constexpr (kShowSettingsPrivacy) {` before the block + matching `}` after the block.
- Every line inside the block gets **one additional tab** of indentation to reflect the new scope.
- Do NOT alter any logic, types, or string literals inside.
- The blank line before the block (line 383) and the blank line after the block (line 390) stay exactly where they are — outside the wrap.
- The "Notifications" block at lines 377-382 (above) and the "Chat Settings" block at lines 391-396 (below) are NOT touched.

### Step 4: Touch the file (WSL mtime defense)

- [ ] Run:

```bash
touch Telegram/SourceFiles/settings/sections/settings_main.cpp
stat -c '%y  %n' Telegram/SourceFiles/settings/sections/settings_main.cpp
```

Expected: mtime is "now". Guards against the WSL/9P mtime-skip bug.

### Step 5: Self-review

- [ ] Constant name is `kShowSettingsPrivacy` (matches the `kShow...` word-order pattern of `kShowUserUsername` / `kShowUserPhone`; not `kShowPrivacy` or `kHidePrivacy`).
- [ ] Constant is `false` (we want to hide).
- [ ] `if constexpr` used (not plain `if`) — guarantees compile-time elimination.
- [ ] Run `grep -c "kShowSettingsPrivacy" Telegram/SourceFiles/settings/sections/settings_main.cpp` — must equal `2` (the constant declaration + the `if constexpr` site).
- [ ] Run `git diff --stat` — must show only `Telegram/SourceFiles/settings/sections/settings_main.cpp`. Expected: `1 file changed, ~10 insertions(+), 0 deletions(-)` (constant + 6-line block wrap means 7 inserted lines wrapping with `if constexpr {` and `}`, plus 6 comment lines before the constant — the inner block lines should appear as context, not as deletions).
- [ ] Run `git diff Telegram/SourceFiles/settings/sections/settings_main.cpp` and confirm exactly two regions are modified:
  1. Insertion at ~line 96 — comment block + `constexpr auto kShowSettingsPrivacy = false;`
  2. At ~lines 384/389 — opening line changes from `\tbuilder.addSectionButton({` to `\tif constexpr (kShowSettingsPrivacy) {` followed by an indented `\t\tbuilder.addSectionButton({` (with all inner lines re-indented by one tab), and a new `\t}` after the existing `\t});`.
- [ ] No other file modified.
- [ ] The "Notifications" block (above) and "Chat Settings" block (below) are unchanged in the diff.

### Step 6: Chinese bullet summary + hand off

Emit:

```
编译前总结（hide Settings → Privacy 入口）：
- settings_main.cpp 匿名命名空间在 kSugValidatePhone 后追加 constexpr auto kShowSettingsPrivacy = false;（带注释，说明 UI-only、deep-link 和 showSettings 仍生效）
- BuildMainSettingsContent 里的 Privacy & Security 入口块（第 384-389 行 builder.addSectionButton({...})）整体用 if constexpr (kShowSettingsPrivacy) { } 包住
- 内部 6 行 brace-init 内容一字未改，每行加一层 tab 缩进
- 无新文件、无新 include、无新 #ifdef gate
- deep-link / 程序化 showSettings(PrivacySecurityId()) / 子页面 parentId 全部不动

请编译并 smoke：
  cmake --build out --config Debug --target Telegram

测试项：
  1. Settings 主菜单 → 没有"隐私与安全"行（Notifications 下面直接是 Chat Settings）
  2. Settings 搜索框输 security / passcode / password / 2fa → 主菜单都搜不到
  3. 其他主菜单行（Notifications / Chat Settings / Folders / Advanced）都正常显示和点击（回归）
  4. tg://settings/privacy_and_security 深链贴到聊天里点击 → Privacy 页面仍能打开（验证 UI-only 范围）
  5. 之前的 hide-user-username-v1 / hide-user-phone-v1 仍生效（相邻特性回归）
```

Wait for "编译通过" + smoke confirmation.

### Step 7: Commit

After user confirms compile + smoke pass:

```bash
git add Telegram/SourceFiles/settings/sections/settings_main.cpp
git commit -m "$(cat <<'EOF'
feat(hide): hide Privacy & Security row in main Settings menu

Add constexpr auto kShowSettingsPrivacy = false; in settings_main.cpp's
anonymous namespace next to kSugValidatePhone, and wrap the Privacy &
Security addSectionButton block with if constexpr (kShowSettingsPrivacy).
UI-only — deep-link routes (tg://settings/privacy_and_security),
programmatic showSettings(PrivacySecurityId()) calls (e.g., the suggest-
archive-and-mute popup), and sub-page parentId registrations are all
untouched. Compile-time elimination, zero runtime cost.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Step 8: Tag (do NOT push without confirmation)

After the commit lands:

```bash
git tag -a hide-settings-privacy-v1 -m "$(cat <<'EOF'
Hide Privacy & Security row in main Settings menu

Mirrors hide-user-username-v1 / hide-user-phone-v1 pattern. Force-hides
the menu entry; deep links and programmatic showSettings remain
functional by design. Settings search keywords (security/passcode/
password/2fa) for this entry also disappear since they live inside the
wrapped block.
EOF
)"
```

Push only on explicit user request.

---

## Spec Coverage Self-Check

| Spec section | Covered by |
|---|---|
| §1 Success criterion 1 (no Privacy row in main menu) | Step 3 wrap |
| §1 Success criterion 2 (search keywords suppressed) | Step 3 wrap (keywords are inside the wrapped block) |
| §1 Success criterion 3 (other rows render unchanged) | Wrap is local to the Privacy block; Notifications above and Chat Settings below are untouched (Step 5 self-review confirms in diff) |
| §1 Success criterion 4 (sub-pages still work via parentId) | Sub-pages live in different files, never touched |
| §1 Success criterion 5 (zero runtime cost) | `if constexpr` compile-time elimination |
| §2 Approach (constexpr + if constexpr wrap) | Step 2 + Step 3 |
| §4 What Is Not Changed (privacy section, deep_links, sub-pages, MTProto) | None of these files appear in Step 5 `git diff --stat` |
| §5 Edge cases (main menu hidden, search no match, programmatic open works, deep link works, sub-page deep links work) | Steps 6 smoke 1-4 cover the user-observable subset; "future re-enable" and "adjacent feature independence" are properties of the chosen pattern |
| §6 Smoke tests 1-8 | Step 6 lists 5 of 8; spec items 5 (Notifications/Chat/Folders/Advanced regression), 6 (suggest-archive popup), 7 (deep link), 8 (hide-user-username/phone regression) all map |
| §7 Risks (upstream rename, block split, search index empty, future deep-link block, if-constexpr discard, constant naming conflict) | Single-line wrap → trivial merge; constants are independent in same anonymous namespace; spec §7 documents these as low/none |
| §8 Implementation Ordering steps 1-9 | Maps to plan steps 1-8 (steps merged: spec §8 step 1 = plan step 1, §8 step 2 = plan step 2, §8 step 3 = plan step 3, §8 step 4 = plan step 4, §8 step 5 = plan step 5, §8 step 6+7 = user compile + smoke between plan steps 6 and 7, §8 step 8 = plan step 7, §8 step 9 = plan step 8) |
