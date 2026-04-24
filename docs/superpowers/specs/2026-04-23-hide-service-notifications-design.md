# Hide Telegram Service Notifications (777000) Design Spec

**Date:** 2026-04-23
**Status:** Approved
**Depends on:** none (independent from employee permission system)
**Reference:** `client/src/global/selectors/chats.ts::selectHiddenChatIds` — the Web companion client's hardcoded hide list (`PERMANENTLY_HIDDEN_USER_IDS` = `{777000}`)

---

## 1. Context & Goal

Telegram sends system messages (login codes, ToS updates, security alerts, changelog, etc.) from a fixed service user — canonically `user_id = 777000`, identified in tdesktop by `PeerData::kServiceNotificationsId`. This fork should hide that user from all user-facing surfaces while keeping messages stored locally and the peer object intact for internal features that depend on it (changelog rendering, background preview, etc.).

The Web companion client already ships this feature. This tdesktop implementation mirrors its scope and adds explicit desktop notification suppression that the Web client doesn't need.

**Success criteria:**
1. The `777000` user never appears in the chat list (main / archive / any filter folder).
2. The `777000` user never appears in search results (local or global).
3. The `777000` user never appears in recipient pickers (forward, add member, share, etc.).
4. Incoming messages from `777000` do not trigger desktop notifications (no tray popup, no sound, no OS notification center entry).
5. Unread messages from `777000` do not inflate the tray badge count.
6. Messages from `777000` are still received and stored locally (can be inspected by future tooling or by lifting the hide in code).
7. The `PeerData::kServiceNotificationsId` constant and related tdesktop-native features (`isNotificationsUser`, `isServiceUser`, changelog sender lookup, etc.) remain untouched.

**Non-goals:**
- Blocking direct deep-link access (`tg://user?id=777000`) — if a user has a link, let them open the conversation. Only the discovery surfaces are hidden.
- Hiding `333000` (archived/legacy "Telegram" account) — per user direction, only `777000` is targeted.
- User-facing toggle or per-employee permission gate — this is a fork-wide default, not gated by `TDESKTOP_EMPLOYEE_MODE`.
- Message-layer drop (messages are stored, only UI-layer hidden).

---

## 2. Architecture

One central predicate + four parallel hook points:

```
┌──────────────────────────────────────────────────────────────┐
│  Data::IsHiddenSystemUser(peer) -> bool                       │
│  peer->id == PeerData::kServiceNotificationsId (hardcoded)    │
└──────────────────────────────────────────────────────────────┘
                            │
        ┌───────────────────┼────────────────────┬──────────────┐
        ▼                   ▼                    ▼              ▼
┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐
│ History::    │  │ Search       │  │ Notification │  │ Recipient    │
│ shouldBeIn   │  │ assembly     │  │ dispatch     │  │ pickers      │
│ ChatList()   │  │              │  │              │  │              │
│              │  │ local +      │  │ Manager::    │  │ Share /      │
│ covers main  │  │ global search│  │ show         │  │ AddParticip. │
│ list, archive│  │ results      │  │ Notification │  │ ChooseRecip. │
│ filter folders│ │              │  │              │  │              │
│ unread badge │  │              │  │              │  │              │
└──────────────┘  └──────────────┘  └──────────────┘  └──────────────┘
```

**Principles:**
- **Single source of truth**: one helper answers "is this peer hidden"; never duplicate the condition
- **Minimal invasion**: a 1-line early-return in `shouldBeInChatList` delivers ~80% of the coverage via the existing hook; three additional explicit checks cover the parallel code paths (search, notification, pickers)
- **No gate**: unconditional fork behavior, no `#ifdef` wrapping (diverges from Phase 1/2 employee code in this respect — see §8 rationale)
- **No MTP/Storage changes**: messages are received and persisted normally

---

## 3. Components

### 3.1 New: `data/data_hidden_peers.h` + `.cpp`

```cpp
// data_hidden_peers.h
#pragma once

#include "base/required.h"

class PeerData;

namespace Data {

// Returns true when the peer should be hidden from user-facing surfaces
// (chat list, search, notifications, recipient pickers). The peer object
// itself, its messages, and internal uses (changelog, background preview,
// etc.) are not affected.
//
// Currently hardcoded: only PeerData::kServiceNotificationsId (777000).
[[nodiscard]] bool IsHiddenSystemUser(not_null<PeerData*> peer);

} // namespace Data
```

```cpp
// data_hidden_peers.cpp
#include "data/data_hidden_peers.h"

#include "data/data_peer.h"

namespace Data {

bool IsHiddenSystemUser(not_null<PeerData*> peer) {
    return peer->id == PeerData::kServiceNotificationsId;
}

} // namespace Data
```

Stateless, no rpl, no dependencies beyond `PeerData`.

### 3.2 Modified: `history/history.cpp` — `History::shouldBeInChatList()`

Insert at the very top of the function body:

```cpp
bool History::shouldBeInChatList() const {
    if (Data::IsHiddenSystemUser(peer)) {
        return false;
    }
    // ... existing logic ...
}
```

This single hook covers:
- Main dialog list (`Dialogs::MainList::addEntry`)
- Archive folder
- Any user-defined filter folder
- Unread badge count accumulation (tray icon badge derives from dialog-list unreads)

### 3.3 Modified: notification dispatch

Primary hook: the desktop notification manager entry — `Window::Notifications::Manager::showNotification` or equivalent. Wrap the body with an early-return:

```cpp
void Manager::showNotification(Notification &&notification) {
    if (Data::IsHiddenSystemUser(notification.item->history()->peer)) {
        return;  // no tray popup, no sound, no OS notification center
    }
    // ... existing dispatch logic ...
}
```

Exact file and function name discovered during implementation (likely `Telegram/SourceFiles/window/notifications_manager.cpp` or similar).

### 3.4 Modified: search result assembly

Two paths in tdesktop search:
- **Local search**: peer index scan in `Dialogs::Searcher` / `Data::Session` contact lists. Filter with `IsHiddenSystemUser` when building the result list.
- **Global (server-side) search**: `messages.searchGlobal` returns a peer list. Filter at the result-integration point in `Api::GlobalSearch` or equivalent.

Implementation scope: one filter site per search path. Exact files located during implementation.

### 3.5 Modified: recipient picker controllers

Peer list controllers driving selection UIs. Expected targets (confirmed during implementation):
- `ChooseRecipientBoxController` (forward messages target)
- `AddParticipantsBoxController` (add member to group/channel)
- `ShareBox` flows that render a peer list
- Any other `PeerListController` subclass that surfaces the full contact/dialog list

Filter pattern: at the point each controller enumerates candidate peers, skip rows where `IsHiddenSystemUser(row_peer)` is true. If a common base class or helper exists, centralize the filter there; otherwise apply per-subclass.

### 3.6 CMake registration

Register the 2 new files in `Telegram/CMakeLists.txt` under the `data/` block alphabetically.

### 3.7 File manifest

| File | New/Modified | Purpose |
|---|---|---|
| `data/data_hidden_peers.h` | New | Predicate declaration |
| `data/data_hidden_peers.cpp` | New | Predicate implementation |
| `Telegram/CMakeLists.txt` | Modified | Register new files |
| `history/history.cpp` | Modified (1 line + 1 include) | `shouldBeInChatList` early-return |
| `window/notifications_manager.cpp` (or analog) | Modified (3-4 lines + 1 include) | Notification dispatch early-return |
| Search result assembly site(s) (1-2 files) | Modified (1-3 lines each + includes) | Filter hidden peers from search results |
| Recipient picker controllers (2-4 files) | Modified (1-3 lines each + includes) | Filter hidden peers from pickers |

**Total estimate: 2 new files + 6-10 modified files + ~15-20 line changes in existing code.**

---

## 4. Data Flow

### 4.1 Message receive path

```
server update_new_message (chat_id=777000)
   → MTP::Session normal processing
   → Data::Session::history(777000) resolves History
   → History::setLastMessage(item)  [message persisted to local DB]
   │
   ├─ History::shouldBeInChatList() returns false
   │  → Dialogs::MainList does NOT addEntry
   │  → archive / filter folders do NOT show this history
   │  → unread badge does NOT accumulate from this history
   │
   ├─ Notifications::Manager::showNotification(item) hits IsHiddenSystemUser
   │  → returns immediately
   │  → no tray popup, no sound, no OS notification center
   │
   └─ local storage: message and History preserved
      UI: peer is invisible to discovery surfaces
```

### 4.2 Search path

```
user types in search box
   → Dialogs::Searcher (local) + Api::searchGlobal (server)
   │
   ├─ local: peer index scan
   │  → filter IsHiddenSystemUser
   │
   └─ global: server returns peer list
      → filter IsHiddenSystemUser at result assembly

   → 777000 absent from merged results
```

### 4.3 Recipient picker path

```
user clicks "Forward message" → ShareBox opens
   → ChooseRecipientBoxController::prepare()
   → enumerates contacts and dialogs
   → per-row check IsHiddenSystemUser — skipped when true
   → picker list omits 777000
```

---

## 5. Edge Cases

| Scenario | Handling |
|---|---|
| 777000 was previously pinned to dialog list | `shouldBeInChatList=false` overrides pinning; pin metadata stays in local state but never materializes in UI. Zero-cost. |
| Deep link `tg://user?id=777000` opens the chat | Not intercepted. Users with a direct link can access the conversation normally — only discovery surfaces are hidden. |
| User actively in 777000 conversation (deep-linked) | All in-chat interactions normal. Hide is strictly discovery-layer. |
| App restart restores the last-open chat = 777000 | Restoration path does not consult `shouldBeInChatList`; chat reopens. But dialog list still omits 777000 (as intended). |
| Another user forwards a 777000 message | "Forwarded from Telegram" attribution renders normally. We do not intercept forward metadata. |
| Server returns 777000 in a contact / global search response | `IsHiddenSystemUser` filter at result-assembly catches it. |
| Tray badge showed unread from 777000 before this change | First launch after this change recomputes badge from dialog list (now excluding 777000). Existing number may "jump" on first launch — acceptable. |
| Phase 1 hard logout / re-login | Clean-slate re-login builds a fresh `Data::Session`. 777000 hide applies identically. |
| Upstream adds new search or picker surfaces | New surface may miss filter. Risk mitigated by the centralized `IsHiddenSystemUser` helper — adding a filter at a new site is one import + one predicate call. |

---

## 6. What Is Not Changed (Explicit Non-Goals)

- **`PeerData::kServiceNotificationsId`** — retained. 20+ tdesktop sites depend on it for "this is a system account" semantics (changelog rendering, background preview, reactions settings, star gifting bypass, etc.).
- **`isServiceUser()` / `isNotificationsUser()`** — retained. These are tdesktop's native "is this a system user" classification, orthogonal to "should it be hidden."
- **MTP layer** — receive / ack / update flows untouched.
- **Storage layer** — messages and `History` objects persist normally. `session->data().peer(777000)` continues to return a valid peer.
- **Changelog delivery** — tdesktop's changelog feature uses `kServiceNotificationsId` as the sender. With 777000 hidden from discovery, users won't see the changelog in their dialog list. Acceptable; users who want changelog can open the conversation via other means.

---

## 7. Testing Strategy (Smoke)

No automated tests (matches Phase 1/2 precedent and tdesktop conventions). Manual smoke checklist after implementation:

1. **Chat list**: Main dialog list, archive, and each filter folder show no "Telegram" entry.
2. **Pinned recovery**: If 777000 was previously pinned, it disappears after rebuild.
3. **Notifications**: Trigger a service message (e.g., sign in from a new device). No tray popup, no sound, no OS notification center entry.
4. **Unread badge**: Tray icon badge number reflects only non-hidden chats.
5. **Search — local**: Search "Telegram" → no 777000 in results.
6. **Search — global**: Search for phrases known to appear in 777000 messages → no 777000 in results.
7. **Recipient pickers**: Forward-to picker, new-group member picker, share-contact picker — none list 777000.
8. **Deep link fallback**: `tg://user?id=777000` still opens the conversation directly.
9. **Messages still exist**: In developer builds or future tooling, verify `session->data().history(peerFromUser(777000))` returns a non-empty history after receiving test messages.

---

## 8. Rationale for Non-Goals and Deviations

### 8.1 Why no `TDESKTOP_EMPLOYEE_MODE` gate?

Employee permissions (Phase 1/2) are per-user backend-controlled behavior. This hide feature is a fork-level customization that applies to every build and every user. Gating it duplicates cost (extra `#ifdef` per site) with zero runtime benefit. Matches the Web client's hardcoded `PERMANENTLY_HIDDEN_USER_IDS` approach.

### 8.2 Why not drop messages at MTP layer?

User explicitly chose "store + hide" over "drop at receive." Rationale: preserves debuggability, mirrors Web client behavior (which relies on React selector filtering, not data-layer exclusion), allows future "reveal" without schema change, and storage impact from 777000 is negligible (the account sends very infrequently).

### 8.3 Why extend `shouldBeInChatList` instead of adding a new filter in MainList::addEntry?

`shouldBeInChatList` is tdesktop's existing designated hook for "should this history participate in any dialog list." Every dialog list consumer already respects it. A custom filter in `MainList` alone would miss archive / filter folders / badge accumulation.

### 8.4 Why not also hide `333000`?

User direction: only `777000`. `333000` is a legacy account rarely active; no behavioral change needed.

---

## 9. Implementation Ordering

Suggested task decomposition (final decisions deferred to plan):

1. Create `data/data_hidden_peers.{h,cpp}` + CMake registration. Compile check.
2. `History::shouldBeInChatList` early-return. Smoke: main list / archive / filter folder hide 777000.
3. Notification dispatch early-return. Smoke: no toast/sound/OS notification for 777000 messages.
4. Search result filter (local + global). Smoke: search returns no 777000.
5. Recipient picker filters. Smoke: pickers omit 777000.
6. Full smoke pass + tag.

Each task begins with a **context analysis step** — grep the upstream file for the actual insertion point, verify no internal assertions depend on the behavior being changed, then apply the minimal edit.

---

## 10. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| An `Expects(history->inChatList())` assertion somewhere triggers when `shouldBeInChatList` returns false for 777000 | Medium | Pre-implementation grep `Expects.*inChatList\|Assert.*inChatList` to find assertion sites; add bypass if 777000 |
| tdesktop's `data_session.cpp:5254` proactive `ensureServiceMessageUser` collides with the hide | Low | That logic ensures the `PeerData` *exists*; we only hide from UI lists. Peer object stays. Compatible. |
| Upstream rebase conflicts on `History::shouldBeInChatList` (commonly modified method) | Medium | One-line insertion at function top; conflicts trivial to hand-resolve. |
| Missed a niche search or picker surface | Low | Centralized `IsHiddenSystemUser` helper: adding a filter at a newly-discovered site is one include + one call. |
| Changelog delivery invisible because its sender is hidden | Known | Accepted trade-off. Users who want changelogs can deep-link or `tg://user?id=777000`. |
