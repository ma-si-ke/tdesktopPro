# Hide Telegram Service Notifications (777000) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Hide user `777000` (Telegram service notifications) from all user-facing surfaces — chat list, archive, filter folders, unread badge, desktop notifications, search results, recipient pickers — while keeping messages stored locally.

**Architecture:** One central predicate `Data::IsHiddenSystemUser(peer)` + four integration points: `History::shouldBeInChatList`, `Window::Notifications::System::skipNotification`, search result filters (local + server), and recipient picker controllers. No `#ifdef` gate — fork-wide default.

**Tech Stack:** C++20, Qt 6.11, tdesktop lib_base idioms. No rpl, no Qt signals — stateless helper + early-returns.

**Spec:** `docs/superpowers/specs/2026-04-23-hide-service-notifications-design.md` (9c595d3)

**Branch baseline:** `customization` @ 9c595d3 (tag `employee-permissions-v2` + spec commit).

---

## Global Conventions

### G1. No `#ifdef TDESKTOP_EMPLOYEE_MODE` wrapping
This feature is fork-wide. All code changes are **unconditional** — no gating.

### G2. Context-first: grep before editing
Every task's Step 1 runs greps to confirm the anchor line numbers haven't drifted since this plan was written. Always trust the anchor string over the line number.

### G3. Chinese bullet summary before compile handoff
After each task's code edits, emit a Chinese bullet summary of what changed, then stop and hand to the user for compile + smoke.

### G4. Workflow
The implementer subagent writes code and self-reviews but does NOT compile or commit. The user compiles on MSVC and smoke-tests; after confirmation, the controller commits.

### G5. Commit message style
Follow Phase 2 precedent: `feat(hide): <terse imperative>`. Trailer:
```
Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
```

---

## File Manifest

### New files (2)
1. `Telegram/SourceFiles/data/data_hidden_peers.h`
2. `Telegram/SourceFiles/data/data_hidden_peers.cpp`

### Modified files (expected)
3. `Telegram/CMakeLists.txt` — register the 2 new files
4. `Telegram/SourceFiles/history/history.cpp` — `shouldBeInChatList` early-return (known location: line 3145)
5. `Telegram/SourceFiles/window/notifications_manager.cpp` — `skipNotification` early-return (known location: line 294)
6. Search result filter sites — 1-2 files, locations discovered during Task 4
7. Recipient picker controllers — known classes `ChooseRecipientBoxController` (peer_list_controllers.h:297), `AddParticipantsBoxController` (add_participants_box.h:20), plus any shared filter base

**Total: 2 new files + 6-10 modified files + ~20 line insertions.**

---

## Task 1: Helper predicate + CMake

### Files
- Create: `Telegram/SourceFiles/data/data_hidden_peers.h`
- Create: `Telegram/SourceFiles/data/data_hidden_peers.cpp`
- Modify: `Telegram/CMakeLists.txt`

### Step 1: Context analysis

- [ ] Confirm `PeerData::kServiceNotificationsId` constant exists:

```bash
grep -n "kServiceNotificationsId" Telegram/SourceFiles/data/data_peer.h
```

Expected: `static constexpr auto kServiceNotificationsId = peerFromUser(777000);` near the top of the class.

- [ ] Check existing `data/` files for style (copyright header, includes, namespace):

```bash
head -15 Telegram/SourceFiles/data/data_peer_values.h
head -15 Telegram/SourceFiles/data/data_peer_values.cpp
```

Expected: copyright block, `#pragma once`, `namespace Data { ... }` pattern.

### Step 2: Create `data_hidden_peers.h`

Write with these exact contents:

```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/required.h"

class PeerData;

namespace Data {

// Returns true when the peer should be hidden from user-facing surfaces
// (chat list, search, notifications, recipient pickers). The peer object,
// its messages, and internal uses (changelog, background preview, etc.)
// are not affected.
//
// Currently hardcoded: only PeerData::kServiceNotificationsId (777000).
[[nodiscard]] bool IsHiddenSystemUser(not_null<PeerData*> peer);

} // namespace Data
```

### Step 3: Create `data_hidden_peers.cpp`

Write with these exact contents:

```cpp
/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "data/data_hidden_peers.h"

#include "data/data_peer.h"

namespace Data {

bool IsHiddenSystemUser(not_null<PeerData*> peer) {
	return peer->id == PeerData::kServiceNotificationsId;
}

} // namespace Data
```

### Step 4: Register in CMake

- [ ] Open `Telegram/CMakeLists.txt` and locate the `data/` block:

```bash
grep -n "data/data_peer.cpp\|data/data_peer_values.cpp" Telegram/CMakeLists.txt | head -5
```

- [ ] Insert the two new entries alphabetically within the `data/` block. Between `data_has_message_with_forum_post_thumb.*` and `data_histories.*` (or equivalent alphabetical slot):

```
data/data_hidden_peers.cpp
data/data_hidden_peers.h
```

### Step 5: Chinese bullet summary + hand off

Emit:

```
编译前总结（Task 1：helper predicate）：
- 新增 data/data_hidden_peers.{h,cpp}，Data::IsHiddenSystemUser(peer) 硬编码判 peer->id == PeerData::kServiceNotificationsId
- CMakeLists.txt 在 data/ 块内按字母序插入两新文件
- 未改任何现有代码
- 注意：新文件入 CMake，请先在 MSVC 重新 configure 工程再编译
请编译验证。
```

Wait for "编译通过".

### Step 6: Commit

After user confirms compile:

```bash
git add Telegram/SourceFiles/data/data_hidden_peers.h \
        Telegram/SourceFiles/data/data_hidden_peers.cpp \
        Telegram/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(hide): add IsHiddenSystemUser predicate

Central predicate for fork-level hiding of system users (initially 777000,
matching client/src/global/selectors/chats.ts PERMANENTLY_HIDDEN_USER_IDS).
Stateless, no rpl.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: History::shouldBeInChatList early-return

### Files
- Modify: `Telegram/SourceFiles/history/history.cpp` (~line 3145)

### Step 1: Context analysis

- [ ] Confirm anchor + line:

```bash
grep -n "^bool History::shouldBeInChatList" Telegram/SourceFiles/history/history.cpp
```

Expected: exactly one hit, line ~3145.

- [ ] Scan for any assertion that might rely on a History being in the chat list — a hidden 777000 History must not trigger these:

```bash
grep -n "Expects.*inChatList\|Assert.*inChatList" Telegram/SourceFiles/
grep -rn "Expects(history->inChatList\|Expects(_history->inChatList\|Assert(history->inChatList" Telegram/SourceFiles/ --include="*.cpp" --include="*.h"
```

Expected: zero or few hits. If any hit references `kServiceNotificationsId`-reachable code paths, report as NEEDS_CONTEXT and stop. Otherwise proceed.

### Step 2: Add include

- [ ] Verify `history.cpp` does NOT already include `data/data_hidden_peers.h`:

```bash
grep -n "data/data_hidden_peers" Telegram/SourceFiles/history/history.cpp
```

Expected: no hits.

- [ ] Near the top of `history.cpp`, alphabetically within the existing `#include "data/..."` block, add:

```cpp
#include "data/data_hidden_peers.h"
```

### Step 3: Insert early-return at top of `shouldBeInChatList`

Current function body (line 3145):

```cpp
bool History::shouldBeInChatList() const {
	if (peer->migrateTo() || !folderKnown()) {
		return false;
	} else if (isPinnedDialog(FilterId())) {
		return true;
	} else if (const auto channel = peer->asChannel()) {
	    ...
	}
	return !lastMessageKnown()
		|| (lastMessage() != nullptr);
}
```

Change to:

```cpp
bool History::shouldBeInChatList() const {
	if (Data::IsHiddenSystemUser(peer)) {
		return false;
	}
	if (peer->migrateTo() || !folderKnown()) {
		return false;
	} else if (isPinnedDialog(FilterId())) {
		return true;
	} else if (const auto channel = peer->asChannel()) {
	    ...
	}
	return !lastMessageKnown()
		|| (lastMessage() != nullptr);
}
```

(Only the first 3 lines are new: the `if (Data::IsHiddenSystemUser(peer)) { return false; }` block at the very top. The rest of the function is unchanged.)

### Step 4: Chinese bullet summary + hand off

```
编译前总结（Task 2：History::shouldBeInChatList）：
- history.cpp:3145 的 shouldBeInChatList 函数体顶部加 IsHiddenSystemUser(peer) 早退
- history.cpp 顶部加 #include "data/data_hidden_peers.h"
- 未改其他逻辑
请编译 + 启动客户端，主聊天列表 / archive / filter folder 应不再显示 Telegram 官方号（777000）；托盘未读数字也应不再算入它。
```

### Step 5: Commit after smoke passes

```bash
git add Telegram/SourceFiles/history/history.cpp
git commit -m "$(cat <<'EOF'
feat(hide): exclude 777000 from chat lists via shouldBeInChatList

One-line early-return at the top of History::shouldBeInChatList covers
main dialog list, archive, filter folders, and unread badge accumulation.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Suppress notifications via skipNotification

### Files
- Modify: `Telegram/SourceFiles/window/notifications_manager.cpp` (~line 294)

### Step 1: Context analysis

- [ ] Confirm anchor:

```bash
grep -n "^System::SkipState System::skipNotification" Telegram/SourceFiles/window/notifications_manager.cpp
```

Expected: exactly one hit, line ~294.

- [ ] Inspect the function body to confirm insertion point:

```bash
sed -n '294,310p' Telegram/SourceFiles/window/notifications_manager.cpp
```

Current body:
```cpp
System::SkipState System::skipNotification(
		Data::ItemNotification notification) const {
	const auto item = notification.item;
	const auto type = notification.type;
	const auto messageType = (type == Data::ItemNotificationType::Message);
	const auto thread = item->maybeNotificationThread();
	if (!thread
		|| !thread->currentNotification()
		|| (messageType && item->skipNotification())
		|| ...) {
		return { SkipState::Skip };
	}
	return computeSkipState(notification);
}
```

### Step 2: Add include

- [ ] Add near the top of `notifications_manager.cpp`, alphabetically within the existing `#include "data/..."` block:

```cpp
#include "data/data_hidden_peers.h"
```

### Step 3: Insert early-return inside `skipNotification`

At the very top of the function body (after the two `const auto item = ...; const auto type = ...;` declarations, but before the existing large `if (...)` return-Skip block), add:

```cpp
System::SkipState System::skipNotification(
		Data::ItemNotification notification) const {
	const auto item = notification.item;
	const auto type = notification.type;
	if (Data::IsHiddenSystemUser(item->history()->peer)) {
		return { SkipState::Skip };
	}
	const auto messageType = (type == Data::ItemNotificationType::Message);
	// ... rest unchanged ...
}
```

This forces every notification for the hidden peer to be treated as Skip — which the caller (`System::schedule` at line 416) handles by calling `thread->popNotification(notification)` and returning without firing.

### Step 4: Chinese bullet summary + hand off

```
编译前总结（Task 3：通知抑制）：
- notifications_manager.cpp 的 System::skipNotification 顶部加 IsHiddenSystemUser 早退（返回 SkipState::Skip）
- 加 gated 之外的普通 #include "data/data_hidden_peers.h"
- 所有下游通知路径（托盘弹窗 / 声音 / OS 通知中心 / 应用内 banner）自动被 skip 机制拦截
请编译 + 用真实账号触发一条 777000 的系统消息（如新设备登录），验证无任何托盘弹窗和声音。
```

### Step 5: Commit after smoke

```bash
git add Telegram/SourceFiles/window/notifications_manager.cpp
git commit -m "$(cat <<'EOF'
feat(hide): suppress desktop notifications from 777000

Extend Window::Notifications::System::skipNotification to return Skip
for hidden system users, preventing tray popup / sound / OS
notification center entry for 777000 messages.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Filter search results

### Files
- Modify: `Telegram/SourceFiles/api/api_peer_search.cpp` (global peer search)
- Modify: any additional search-result assembly site discovered in Step 1

### Step 1: Context analysis

The spec lists two search paths: local (peer index) and global (server `messages.searchGlobal` / `contacts.search`). The tdesktop implementation splits across:
- `api/api_peer_search.cpp` — server-side peer search (global)
- `dialogs/dialogs_inner_widget.cpp` — main dialog-list search filter (local)
- `data/data_search_controller.cpp` — shared search controller for messages/peers

Run these to locate the peer-result assembly points:

```bash
grep -n "peers.emplace_back\|chats.emplace_back\|results.push_back\|_peers\." Telegram/SourceFiles/api/api_peer_search.cpp | head -20

grep -n "search\|filter\|peerData->\|isServiceUser\|kServiceNotifications" Telegram/SourceFiles/api/api_peer_search.cpp | head -30

grep -rn "contacts.search\|messages.searchGlobal\|_myResults\|_peerResults\|_searchResults" Telegram/SourceFiles/api/ Telegram/SourceFiles/dialogs/ --include="*.cpp" | head -20
```

- [ ] Identify the concrete point(s) where a server-returned peer list is converted into result rows. Typically `Api::PeerSearch::request(...)` or a callback inside it receives `MTPcontacts_Found` / `MTPmessages_PeerSearch` and appends entries.

- [ ] Also identify the local-search filter in `dialogs_inner_widget.cpp`. Grep for where contacts are enumerated for the search-bar filtering:

```bash
grep -n "filterHash\|filteredPeers\|searchInPeer\|ContactsFilter" Telegram/SourceFiles/dialogs/dialogs_inner_widget.cpp | head -20
```

If the code uses a common helper like `History::shouldBeInChatList()` for the local list (which Task 2 already covers), no additional change may be needed for local search — the local search scans the same indexed list that Task 2's hook already filters. Confirm this claim by inspecting the filter loop and report the finding.

### Step 2: Add filter — global peer search result

Assume the result-assembly loop in `api_peer_search.cpp` (or analog) looks like:

```cpp
for (const auto &mtpPeer : data.vpeers().v) {
    const auto peer = session->data().peerLoaded(peerFromMTP(mtpPeer));
    if (peer) {
        peers.push_back(peer);
    }
}
```

Change to:

```cpp
for (const auto &mtpPeer : data.vpeers().v) {
    const auto peer = session->data().peerLoaded(peerFromMTP(mtpPeer));
    if (peer && !Data::IsHiddenSystemUser(peer)) {
        peers.push_back(peer);
    }
}
```

The exact variable names and container type will differ; adapt to what you find. The key pattern: wherever a peer is about to be added to a search-result collection, skip if `IsHiddenSystemUser`.

### Step 3: If needed, filter local search

If Step 1 revealed a local-search site that does NOT already go through `shouldBeInChatList`, add a similar filter there. Otherwise no change.

### Step 4: Add include

Add `#include "data/data_hidden_peers.h"` to each modified file.

### Step 5: Self-review checklist

- [ ] Every search-result container append site checked.
- [ ] No existing filter logic disturbed — only one extra `&& !Data::IsHiddenSystemUser(peer)` condition added.
- [ ] If both local and global paths are covered by the same helper, document that in the commit message.

### Step 6: Chinese bullet summary + hand off

```
编译前总结（Task 4：搜索结果过滤）：
- api_peer_search.cpp（或对应 server-side peer search 汇总处）的 peer 列表构造循环里加 !IsHiddenSystemUser 过滤
- 若本地搜索未被 Task 2 的 shouldBeInChatList 钩子覆盖，另补一处（实际位点视 grep 结果）
- 加 gated 之外的普通 include
请编译 + 在搜索框输入 "Telegram" / "service" 等关键词，验证结果里无 777000。
```

### Step 7: Commit after smoke

```bash
git add <files>
git commit -m "$(cat <<'EOF'
feat(hide): exclude 777000 from search results

Filter IsHiddenSystemUser at peer-result assembly for global peer
search (and local search if not already covered by Task 2's
shouldBeInChatList hook).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Filter recipient pickers

### Files
- Modify: `Telegram/SourceFiles/boxes/peer_list_controllers.cpp` (ChooseRecipientBoxController — forward target picker)
- Modify: `Telegram/SourceFiles/boxes/peers/add_participants_box.cpp` (AddParticipantsBoxController — group member adder)
- Modify: `Telegram/SourceFiles/boxes/share_box.cpp` (share picker, if it enumerates peers directly)
- Additional recipient/peer list surfaces discovered via grep in Step 1

### Step 1: Context analysis

```bash
# Find where ChooseRecipientBoxController populates its rows
grep -n "ChooseRecipientBoxController::\|rebuildRows\|prepareViewWithNoRows\|populateFromList\|delegate()->peerListAppendRow" \
     Telegram/SourceFiles/boxes/peer_list_controllers.cpp | head -20

# AddParticipantsBoxController
grep -n "AddParticipantsBoxController::\|populateFromContacts\|prepare\b\|rebuildRows" \
     Telegram/SourceFiles/boxes/peers/add_participants_box.cpp | head -20

# ContactsBoxController — often the common base
grep -n "ContactsBoxController::\|populateFrom\|prepareViewWithNoRows" \
     Telegram/SourceFiles/boxes/peer_list_controllers.cpp | head -15

# Broad: any peerListAppendRow near a filter/skip comment
grep -rn "peerListAppendRow\|createRow.*peer" Telegram/SourceFiles/boxes/ --include="*.cpp" | head -20

# Specific: isSelf / isBot early-skips already in place — we add next to them
grep -rn "if (user->isSelf())\|if (peer->isSelf())\|continue.*isSelf\|isServiceUser()" Telegram/SourceFiles/boxes/ --include="*.cpp" | head -15
```

Expected output: sites where `for (const auto &peer : peers) { ... peerListAppendRow(createRow(peer)); }` or similar appears. Each such site gets `if (Data::IsHiddenSystemUser(peer)) continue;` added right before the append.

### Step 2: Insert filter at each site

For each identified site, apply this pattern:

Before:
```cpp
for (const auto &peer : peers) {
    if (peer->isSelf()) continue;  // existing skips
    delegate()->peerListAppendRow(createRow(peer));
}
```

After:
```cpp
for (const auto &peer : peers) {
    if (peer->isSelf()) continue;  // existing skips
    if (Data::IsHiddenSystemUser(peer)) continue;
    delegate()->peerListAppendRow(createRow(peer));
}
```

(The `if (peer->isSelf())` skip is a placeholder — use whatever existing skip pattern the file uses. The key: add the `IsHiddenSystemUser` skip adjacent to existing filters.)

### Step 3: If `ContactsBoxController` is a shared base with a common populate method, prefer centralizing there

If inspection reveals that `ChooseRecipientBoxController`, `AddParticipantsBoxController`, and similar all derive from `ContactsBoxController` and inherit a populate method, add the filter once in the base. Otherwise apply per-subclass.

### Step 4: Add include to each modified file

```cpp
#include "data/data_hidden_peers.h"
```

### Step 5: Self-review

- [ ] Every picker that surfaces a full user/contact list has the filter.
- [ ] Pickers that target specific peer categories (admins, bots, channel members) may NOT need the filter — system user 777000 isn't in those categories anyway. Judgment call: skip if it can never appear.
- [ ] Share box (`share_box.cpp`) enumerates recent dialogs → filtered by Task 2's `shouldBeInChatList` hook? Verify. If yes, no extra change.

### Step 6: Chinese bullet summary + hand off

```
编译前总结（Task 5：recipient picker 过滤）：
- ChooseRecipientBoxController（转发目标选择）加过滤
- AddParticipantsBoxController（加成员）加过滤
- 若 ContactsBoxController 是共享基类，集中在那里；否则每个子类单独加
- ShareBox 若依赖 shouldBeInChatList，已被 Task 2 覆盖；否则补
- 加 include
请编译 + 试这几个场景：右键消息 → 转发 → 目标选择框无 Telegram；新建群组 → 成员选择框无 Telegram；分享联系人/文件 → 目标选择框无 Telegram。
```

### Step 7: Commit after smoke

```bash
git add <files>
git commit -m "$(cat <<'EOF'
feat(hide): exclude 777000 from recipient pickers

Filter IsHiddenSystemUser in ChooseRecipientBoxController,
AddParticipantsBoxController, and any shared peer list controller
base. Prevents 777000 from appearing as a forward/share/add-member
target.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Full smoke + tag

### Step 1: Full smoke matrix

- [ ] Ask user to verify each item:

| # | Scenario | Expected |
|---|---|---|
| 1 | Main dialog list | No "Telegram" entry |
| 2 | Archive folder | No "Telegram" entry |
| 3 | Each custom filter folder | No "Telegram" entry |
| 4 | Tray unread badge | Number excludes any 777000 unread |
| 5 | Trigger a 777000 message (re-login from another device, change settings, etc.) | No tray popup, no sound, no OS notification |
| 6 | Local search: type "Telegram" | No 777000 in results |
| 7 | Global search: type something the 777000 user has said | No 777000 in results |
| 8 | Forward a message → target picker | No 777000 |
| 9 | New group → add member picker | No 777000 |
| 10 | Share contact/file → target picker | No 777000 |
| 11 | Deep link `tg://user?id=777000` | Chat opens normally (NOT intercepted — verify unchanged) |
| 12 | Open the 777000 chat via deep link and verify messages still load | Messages visible in-chat, confirming data layer intact |

### Step 2: Tag and push

- [ ] After all smoke passes, create the annotated tag and push:

```bash
git tag -a hide-service-notifications-v1 -m "$(cat <<'EOF'
Hide Telegram service notifications (777000) from user-facing surfaces

- Central Data::IsHiddenSystemUser predicate
- Exclusion from chat list / archive / filter folders / unread badge
  via History::shouldBeInChatList
- Desktop notification suppression via
  Window::Notifications::System::skipNotification
- Search results filter at Api::PeerSearch and local search
- Recipient picker filter (ChooseRecipientBoxController,
  AddParticipantsBoxController)
- No #ifdef gate; fork-wide default
- Messages still received and persisted; deep-link access preserved
EOF
)"

git push origin customization
git push origin hide-service-notifications-v1
```

Require explicit user confirmation before pushing.

---

## Spec Coverage Self-Check

| Spec section | Covered by |
|---|---|
| §2 Predicate `Data::IsHiddenSystemUser` | Task 1 |
| §2 `History::shouldBeInChatList` hook | Task 2 |
| §2 Notification dispatch hook | Task 3 |
| §2 Search result filter | Task 4 |
| §2 Recipient picker filter | Task 5 |
| §3 Data flow correctness | Tasks 2, 3 preserve message receive + persistence |
| §4 Message receive path | Unchanged (no MTP/Storage mod) |
| §5 Edge cases: deep link, pinned, forwarded attribution | Verified in Task 6 smoke items 11-12 |
| §6 Non-goals: `kServiceNotificationsId` constant, `isServiceUser`, MTP layer | All untouched |
| §7 Testing strategy | Task 6 matrix mirrors spec §7 checklist |
| §8 Rationale for non-gate | G1 convention |
| §9 Implementation ordering | Tasks 1→6 in order matches spec §9 |
| §10 Risk: `Expects(inChatList)` | Task 2 Step 1 pre-check |
| §10 Risk: tdesktop `ensureServiceMessageUser` | Unchanged, compatible (Task 2 only hides UI, peer still exists) |
| §10 Risk: upstream merge conflicts | Minimal one-line insertions, easy to hand-resolve |
