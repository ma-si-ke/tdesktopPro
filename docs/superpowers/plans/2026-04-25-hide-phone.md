# Hide Phone Row in User Profile Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hide the phone-number row (and its right-click "Copy phone" / "Add to contacts" hooks) in the user profile info page. Settings main page (self phone), shared-contact message bubbles, short-info popup, and edit-contact dialog are unaffected.

**Architecture:** Single-file change — add a file-scope `constexpr` constant `kShowUserPhone = false` next to the existing `kShowUserUsername`, and convert the bare `{ }` scope around the phone block in `DetailsFiller::setupInfo()` into `if constexpr (kShowUserPhone) { }`. Mirrors the Web client's `const SHOW_PHONE_NUMBER = false;` and the existing `hide-user-username-v1` pattern 1:1. Compile-time elimination, zero runtime cost.

**Tech Stack:** C++17 `if constexpr`.

**Spec:** `docs/superpowers/specs/2026-04-25-hide-phone-design.md` (f2dabcb)

**Branch baseline:** `customization` @ f2dabcb.

---

## Global Conventions

### G1. No `#ifdef TDESKTOP_EMPLOYEE_MODE` wrapping
Fork-wide default. Plain C++.

### G2. No new files, no new includes
Single-file edit.

### G3. Chinese bullet summary before compile handoff
Emit a Chinese bullet summary of what changed, then stop and hand to the user for compile + smoke.

### G4. Workflow
The implementer subagent writes code and self-reviews but does NOT compile or commit. The user compiles on MSVC and smoke-tests; after confirmation, the controller commits.

### G5. Commit message style
```
feat(hide): hide phone row in user profile

<body>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

### G6. Mtime touch on edited files
After every Edit/Write, run `touch <path>` on the file. WSL on `/mnt/d` (NTFS via 9P) is known to skip mtime updates, which makes MSBuild think the source is already built. The user has hit this twice in this session; touching defends against silent "no rebuild" failures.

---

## Task 1: Add constexpr + wrap phone block

### File
Modify: `Telegram/SourceFiles/info/profile/info_profile_actions.cpp` (single file, 2 hunks)

### Step 1: Context analysis

- [ ] Confirm anchor lines and the phone-block structure:

```bash
grep -n "^namespace {\|^constexpr auto kDay\|^constexpr auto kShowUserUsername\|const auto phoneLabel = addInfoOneLine" Telegram/SourceFiles/info/profile/info_profile_actions.cpp | head -10
```

Expected (against current `customization` HEAD f2dabcb):
- `namespace {` at line 116
- `constexpr auto kDay = Data::WorkingInterval::kDay;` at line 118
- `constexpr auto kShowUserUsername = false;` at line 124 (where the new constant goes after)
- `const auto phoneLabel = addInfoOneLine(` at line 1654 (inside the bare `{` at 1653)

- [ ] Read lines 1652-1676 to confirm exact block boundaries:

```bash
sed -n '1652,1676p' Telegram/SourceFiles/info/profile/info_profile_actions.cpp
```

The block opens with a lone `{` at line 1653 and closes with `}` at line 1675. Inside is `phoneLabel`, the `hook` lambda, and `phoneLabel->setContextMenuHook(hook);`. The next statement after `}` is `auto label = user->isBot()` at line 1676 — that line stays OUTSIDE the wrap.

### Step 2: Add file-scope constant

- [ ] Insert in the anonymous namespace immediately after the existing `kShowUserUsername` block (after line 124). Following the same paragraph style: blank line separator, comment block, constant. Final shape of lines 124-130 should be:

```cpp
constexpr auto kShowUserUsername = false;

// Hide phone-number row (and its copy / add-contact context-menu hooks)
// in user profile. Matches Web client's SHOW_PHONE_NUMBER = false in
// client/src/components/common/profile/ChatExtra.tsx.
// Self phone in Settings, shared-contact message bubble, short-info popup,
// and edit-contact dialog are NOT affected.
constexpr auto kShowUserPhone = false;
```

The blank line and the existing `base::options::toggle ShowPeerIdBelowAbout({` that follows stay unchanged.

### Step 3: Convert the bare `{ }` scope into `if constexpr (kShowUserPhone) { ... }`

- [ ] Locate the block at line 1653-1675. Current code (preserved tab indentation, 2 leading tabs for the outer scope):

```cpp
		{
			const auto phoneLabel = addInfoOneLine(
				tr::lng_info_mobile_label(),
				PhoneOrHiddenValue(user),
				tr::lng_profile_copy_phone(tr::now)).text;
			const auto hook = [=](Ui::FlatLabel::ContextMenuRequest request) {
				if (request.selection.empty()) {
					const auto callback = [=] {
						auto phone = rpl::variable<TextWithEntities>(
							PhoneOrHiddenValue(user)).current().text;
						phone.replace(' ', QString()).replace('-', QString());
						TextUtilities::SetClipboardText({ phone });
					};
					request.menu->addAction(
						tr::lng_profile_copy_phone(tr::now),
						callback);
				} else {
					phoneLabel->fillContextMenu(request);
				}
				AddPhoneMenu(request.menu, user);
			};
			phoneLabel->setContextMenuHook(hook);
		}
```

- [ ] Replace ONLY the lone opening `{` on line 1653 with `if constexpr (kShowUserPhone) {`. Closing `}` on line 1675 stays unchanged. Final form:

```cpp
		if constexpr (kShowUserPhone) {
			const auto phoneLabel = addInfoOneLine(
				tr::lng_info_mobile_label(),
				PhoneOrHiddenValue(user),
				tr::lng_profile_copy_phone(tr::now)).text;
			const auto hook = [=](Ui::FlatLabel::ContextMenuRequest request) {
				if (request.selection.empty()) {
					const auto callback = [=] {
						auto phone = rpl::variable<TextWithEntities>(
							PhoneOrHiddenValue(user)).current().text;
						phone.replace(' ', QString()).replace('-', QString());
						TextUtilities::SetClipboardText({ phone });
					};
					request.menu->addAction(
						tr::lng_profile_copy_phone(tr::now),
						callback);
				} else {
					phoneLabel->fillContextMenu(request);
				}
				AddPhoneMenu(request.menu, user);
			};
			phoneLabel->setContextMenuHook(hook);
		}
```

**Key constraints:**
- Only one line changes inside the block: `\t\t{` → `\t\tif constexpr (kShowUserPhone) {`. Every other line is byte-for-byte identical.
- **Do NOT add an outer extra brace pair.** The existing `{ }` is already at the correct depth and was a bare scope just for `phoneLabel` / `hook` variable lifetimes; converting it to `if constexpr` preserves both purposes (variable scope still local; block is now elidable). No re-indentation needed.
- Do NOT alter any logic, types, or variable names inside.
- The line `auto label = user->isBot()` immediately after the closing `}` (line 1676 currently) stays outside the wrap.

### Step 4: Touch the file (WSL mtime defense)

- [ ] Run:

```bash
touch Telegram/SourceFiles/info/profile/info_profile_actions.cpp
stat -c '%y  %n' Telegram/SourceFiles/info/profile/info_profile_actions.cpp
```

Expected: mtime is "now". This guards against the WSL/9P mtime-skip bug that has bitten this session twice.

### Step 5: Self-review

- [ ] Constant name is `kShowUserPhone` (matches `kShowUserUsername` word order; not `kShowPhone` or `kHidePhone`).
- [ ] Constant is `false` (we want to hide).
- [ ] `if constexpr` used (not plain `if`) — guarantees compile-time elimination.
- [ ] Block wrapping is the converted scope brace, NOT a new outer wrapper. Run `grep -c "if constexpr (kShowUserPhone)" Telegram/SourceFiles/info/profile/info_profile_actions.cpp` — must equal `1`.
- [ ] No re-indentation inside the block. Run `git diff Telegram/SourceFiles/info/profile/info_profile_actions.cpp` and confirm only TWO regions are modified: the constant insertion near line 124 and the single line at 1653 changing from `\t\t{` to `\t\tif constexpr (kShowUserPhone) {`.
- [ ] The `if (const auto user = _peer->asUser()) {` outer scope (line ~1646) and the `auto label = user->isBot()` line (line ~1676) are both UNTOUCHED.
- [ ] No other file modified. Run `git diff --stat` — must show only `info_profile_actions.cpp`.

### Step 6: Chinese bullet summary + hand off

Emit:

```
编译前总结（hide phone）：
- info_profile_actions.cpp 匿名命名空间在 kShowUserUsername 后追加 constexpr auto kShowUserPhone = false;（带注释，对齐 Web client SHOW_PHONE_NUMBER = false）
- 用户 profile DetailsFiller::setupInfo() 内电话块的开头那个裸 `{`（第 1653 行）改成 `if constexpr (kShowUserPhone) {`，闭合 `}` 不动；块内一行未改、缩进不变
- 无新文件、无 include、无 #ifdef gate
- 自己的设置页号码、聊天里的联系人名片消息、短信息浮窗、编辑联系人对话框 全部保持显示
请编译并 smoke：
  1. 任何联系人/非联系人/bot profile → 没有 Mobile 行，右键也没"Copy phone"/"Add to contacts"
  2. Settings → 主页 → 自己的号码仍然显示（回归）
  3. 聊天里收/发联系人名片消息 → 名片气泡上号码仍然显示（回归）
  4. 公开频道/群 profile → t.me 邀请链接行仍显示（回归）
  5. hide-user-username-v1 仍生效 → @username 行仍隐藏（相邻常量回归）
```

Wait for "编译通过" + smoke confirmation.

### Step 7: Commit

```bash
git add Telegram/SourceFiles/info/profile/info_profile_actions.cpp
git commit -m "$(cat <<'EOF'
feat(hide): hide phone row in user profile

Add constexpr auto kShowUserPhone = false; in info_profile_actions.cpp's
anonymous namespace next to kShowUserUsername, and convert the bare
scope brace around the phone block in DetailsFiller::setupInfo() into
if constexpr (kShowUserPhone) { ... }. Matches Web client
SHOW_PHONE_NUMBER = false in ChatExtra.tsx. Self phone in Settings,
shared-contact message bubble, short-info popup, and edit-contact
dialog are unaffected. Compile-time elimination, zero runtime cost.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Step 8: Tag (do NOT push without confirmation)

After the commit lands:

```bash
git tag -a hide-user-phone-v1 -m "$(cat <<'EOF'
Hide phone-number row in user profile info page

Mirrors Web client's SHOW_PHONE_NUMBER = false. Fork-wide default
(no #ifdef gate). User profile hides phone row + its right-click
"Copy phone" / "Add to contacts" hooks. Self phone in Settings,
shared-contact message bubble, short-info popup, and edit-contact
dialog unaffected.
EOF
)"
```

Push only on explicit user request (user pushes batches manually).

---

## Spec Coverage Self-Check

| Spec section | Covered by |
|---|---|
| §1 Success criterion 1 (no Mobile row in user profile) | Step 3 conversion |
| §1 Success criterion 2 (copy / add-contact menu hooks unreachable) | Step 3 (the `setContextMenuHook` call is inside the wrapped block) |
| §1 Success criterion 3 (other info unchanged) | Conversion limited to the phone-block scope brace; bio / username / common-groups / personal-channel / status code untouched |
| §1 Success criterion 4 (group/channel info untouched) | Phone block already lives inside `if (const auto user = _peer->asUser())`; group/channel branch never hits it |
| §1 Success criterion 5 (Settings self phone preserved) | `settings/sections/settings_main.cpp` is a separate file, not modified |
| §1 Success criterion 6 (zero runtime cost) | `if constexpr` compile-time elimination |
| §2 Approach (constexpr + if constexpr wrap) | Step 2 + Step 3 |
| §4 What is not changed (PhoneValue/PhoneOrHiddenValue, FormatPhone, AddPhoneMenu definition, UserData::phone(), group/channel, Settings) | Confirmed in Step 5 self-review (`git diff --stat` shows only one file) |
| §5 Edge cases (no phone, bot, self via Info, right-click on absent row, group/channel, add-contact via other paths, contact-card message) | All covered by block-level hide; absent row implies absent menu; other paths/files untouched |
| §6 Smoke tests 1-9 | Step 6 Chinese summary lists items 1-5 (the high-value ones); items 6-9 (chat top bar regression, public channel info regression, hide-user-username-v1 regression) covered by item 4 + 5 |
| §7 Risks (upstream refactor, layout gap, AddPhoneMenu reference, runtime toggle, conflict with username constant) | Single-line conversion → trivial merge; layout gap N/A (same as username case); `AddPhoneMenu` definition stays in `info_profile_phone_menu.cpp`; constants are independent |
| §8 Implementation Ordering | Step 1 (context) → Step 2 (constant) → Step 3 (wrap) → Step 4 (mtime) → Step 5 (self-review) → Step 6 (handoff) → Step 7 (commit) → Step 8 (tag) |
