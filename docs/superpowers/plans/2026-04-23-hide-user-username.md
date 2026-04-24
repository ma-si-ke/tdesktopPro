# Hide @username Row in User Profile Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hide the `@username` row and its adjacent QR button / secondary-username sub-links in the user profile info page. Group / channel invite-link row unaffected.

**Architecture:** Single-file change — add a file-scope `constexpr` constant and wrap the existing username block with `if constexpr (...)`. Mirrors the Web client's `const SHOW_USERNAME = false;` pattern 1:1. Compile-time elimination, zero runtime cost.

**Tech Stack:** C++17 `if constexpr`.

**Spec:** `docs/superpowers/specs/2026-04-23-hide-user-username-design.md` (8d0349a)

**Branch baseline:** `customization` @ 8d0349a.

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
feat(hide): hide @username row in user profile

<body>

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## Task 1: Add constexpr + wrap username block

### File
Modify: `Telegram/SourceFiles/info/profile/info_profile_actions.cpp` (single file, 2 hunks)

### Step 1: Context analysis

- [ ] Confirm anonymous namespace and the username block location:

```bash
grep -n "^namespace {\|constexpr auto kDay\|const auto usernameLine = addInfoOneLine\|UsernamesSubtext(_peer, tr::lng_info_username_label" Telegram/SourceFiles/info/profile/info_profile_actions.cpp | head -10
```

Expected:
- `namespace {` at line ~116
- `constexpr auto kDay = Data::WorkingInterval::kDay;` at line ~118 (first constant in that namespace)
- `const auto usernameLine = addInfoOneLine(` at line ~1677 (the block to wrap)

- [ ] Read lines 1676-1712 to confirm block structure:

```bash
sed -n '1676,1712p' Telegram/SourceFiles/info/profile/info_profile_actions.cpp
```

The block begins at `const auto usernameLine = addInfoOneLine(...)` and ends just before the `if (!user->isBot()) {` line. All lines in between (including the QR button setup at ~1695-1708) are part of the block.

### Step 2: Add file-scope constant

- [ ] Insert at the top of the anonymous namespace, immediately after `constexpr auto kDay = Data::WorkingInterval::kDay;` (line 118):

```cpp
// Hide @username row (and its QR button / secondary usernames) in user
// profile. Matches Web client's SHOW_USERNAME = false in
// client/src/components/common/profile/ChatExtra.tsx.
// Group/channel invite-link row is NOT affected.
constexpr auto kShowUserUsername = false;
```

Use tab indent matching the surrounding declarations. Blank line after for readability (`kDay` has a blank line after it).

### Step 3: Wrap the username block

- [ ] Locate the block at ~line 1677. Current code:

```cpp
		const auto usernameLine = addInfoOneLine(
			UsernamesSubtext(_peer, tr::lng_info_username_label()),
			UsernameValue(user, true) | rpl::map([=](TextWithEntities u) {
				return u.text.isEmpty()
					? TextWithEntities()
					: tr::link(u, UsernameUrl(user, u.text.mid(1)));
			}),
			QString(),
			st::infoProfileLabeledUsernamePadding);
		const auto callback = UsernamesLinkCallback(
			_peer,
			controller,
			QString());
		usernameLine.text->overrideLinkClickHandler(callback);
		usernameLine.subtext->overrideLinkClickHandler(callback);
		usernameLine.text->setContextMenuHook(lnkHook);
		usernameLine.subtext->setContextMenuHook(lnkHook);

		const auto qrButton = Ui::CreateChild<Ui::IconButton>(
			usernameLine.text->parentWidget(),
			st::infoProfileLabeledButtonQr);
		qrButton->setAccessibleName(tr::lng_group_invite_context_qr(tr::now));
		UsernamesValue(_peer) | rpl::on_next([=](const auto &u) {
			qrButton->setVisible(!u.empty());
		}, qrButton->lifetime());
		const auto rightSkip = st::infoProfileLabeledButtonQrRightSkip;
		fitLabelToButton(qrButton, usernameLine.text, rightSkip);
		fitLabelToButton(qrButton, usernameLine.subtext, rightSkip);
		qrButton->setClickedCallback([=, show = controller->uiShow()] {
			Ui::DefaultShowFillPeerQrBoxCallback(show, user);
			return false;
		});
```

- [ ] Wrap it in `if constexpr (kShowUserUsername) { ... }`. Final form:

```cpp
		if constexpr (kShowUserUsername) {
			const auto usernameLine = addInfoOneLine(
				UsernamesSubtext(_peer, tr::lng_info_username_label()),
				UsernameValue(user, true) | rpl::map([=](TextWithEntities u) {
					return u.text.isEmpty()
						? TextWithEntities()
						: tr::link(u, UsernameUrl(user, u.text.mid(1)));
				}),
				QString(),
				st::infoProfileLabeledUsernamePadding);
			const auto callback = UsernamesLinkCallback(
				_peer,
				controller,
				QString());
			usernameLine.text->overrideLinkClickHandler(callback);
			usernameLine.subtext->overrideLinkClickHandler(callback);
			usernameLine.text->setContextMenuHook(lnkHook);
			usernameLine.subtext->setContextMenuHook(lnkHook);

			const auto qrButton = Ui::CreateChild<Ui::IconButton>(
				usernameLine.text->parentWidget(),
				st::infoProfileLabeledButtonQr);
			qrButton->setAccessibleName(tr::lng_group_invite_context_qr(tr::now));
			UsernamesValue(_peer) | rpl::on_next([=](const auto &u) {
				qrButton->setVisible(!u.empty());
			}, qrButton->lifetime());
			const auto rightSkip = st::infoProfileLabeledButtonQrRightSkip;
			fitLabelToButton(qrButton, usernameLine.text, rightSkip);
			fitLabelToButton(qrButton, usernameLine.subtext, rightSkip);
			qrButton->setClickedCallback([=, show = controller->uiShow()] {
				Ui::DefaultShowFillPeerQrBoxCallback(show, user);
				return false;
			});
		}
```

**Key constraints:**
- Only change: add `if constexpr (kShowUserUsername) {` before the block + `}` after the block.
- Every line inside the block gets **one additional tab** of indentation to reflect the new scope.
- The blank line between the `setContextMenuHook` section and `const auto qrButton` stays blank (just indented).
- Do NOT alter any logic, types, or variable names.

### Step 4: Self-review

- [ ] Constant name is `kShowUserUsername` (not `kShowUsername` or `kHideUsername` — word order matters for the planning consistency).
- [ ] Constant is `false` (we want to hide).
- [ ] `if constexpr` used (not plain `if`) — guarantees compile-time elimination.
- [ ] Block wrapping is contiguous — no code between `const auto usernameLine = ...` and the last `setClickedCallback(...)` closing `});` is outside the wrap.
- [ ] The `if (!user->isBot()) {` that immediately follows (at ~line 1710) is NOT included in the wrap — it's a separate block handling bio extras.
- [ ] Indentation: every wrapped line gets exactly 1 additional tab relative to its old indent.
- [ ] No other file touched.

### Step 5: Chinese bullet summary + hand off

Emit:

```
编译前总结（hide @username）：
- info_profile_actions.cpp 匿名命名空间加 constexpr auto kShowUserUsername = false;
- 用户 profile 里 usernameLine + callback + context menu + QR 按钮 + rpl 订阅 整块（~1677-1708 行）用 if constexpr (kShowUserUsername) { } 包住
- 每行加一层 tab 缩进，内部逻辑一字未改
- 无新文件、无 include 变动、无 #ifdef gate
请编译并 smoke：
  1. 打开任何联系人/非联系人/bot profile → 无 @username 行，无右侧 QR 按钮
  2. 公开频道/群 profile → t.me 邀请链接行仍然显示（不应被影响）
  3. @mention 点击跳转到 profile 后，profile 里也没有 @username 行（一致）
  4. 聊天顶栏、消息中 @mention 链接正常工作
```

Wait for "编译通过" + smoke confirmation.

### Step 6: Commit

```bash
git add Telegram/SourceFiles/info/profile/info_profile_actions.cpp
git commit -m "$(cat <<'EOF'
feat(hide): hide @username row in user profile

Wrap the user-profile @username block (addInfoOneLine + link callback +
context menu hook + QR button + visibility rpl subscription) with
if constexpr (kShowUserUsername) in info_profile_actions.cpp. Constant
is hardcoded false, matching client/src/components/common/profile/
ChatExtra.tsx SHOW_USERNAME = false. Group/channel invite-link row
unaffected. Compile-time elimination, zero runtime cost.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

### Step 7: Tag + push

After the commit lands:

```bash
git tag -a hide-user-username-v1 -m "$(cat <<'EOF'
Hide @username row in user profile info page

Mirrors Web client's SHOW_USERNAME = false. Fork-wide default (no
#ifdef gate). User profile hides @username row + adjacent QR button;
group/channel invite-link row unaffected.
EOF
)"

git push origin customization
git push origin hide-user-username-v1
```

Require explicit user confirmation before pushing.

---

## Spec Coverage Self-Check

| Spec section | Covered by |
|---|---|
| §1 Success criterion 1 (no @username row in user profile) | Step 3 wrap |
| §1 Success criterion 2 (QR + sub-links hidden) | Step 3 wrap (they are inside the wrapped block) |
| §1 Success criterion 3 (other info unchanged) | Block doesn't touch name/bio/phone/common-groups/personal-channel/status code paths |
| §1 Success criterion 4 (group/channel invite-link preserved) | Different branch in the file, not touched |
| §1 Success criterion 5 (top bar / in-message mentions unchanged) | Not in this file |
| §1 Success criterion 6 (zero runtime cost) | `if constexpr` compile-time elimination |
| §2 Approach (constexpr + wrap) | Step 2 + Step 3 |
| §4 Non-goals (phone hide, @mention clickability, username elsewhere) | Explicitly NOT touched; confirmed in Step 4 self-review |
| §5 Edge cases | All covered by block-level hide (user has no username, bot, self, multi-username, QR) |
| §6 Smoke tests | Step 5 includes 4 of the 7 spec smoke items. Items 5-6 (channel/group invite-link regression) are in Step 5. Item 7 (mention link works) is also in Step 5. |
| §7 Risks | Mitigated by contiguous wrap (Step 4 self-review #4) |
