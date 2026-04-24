# Hide @username Row in User Profile Design Spec

**Date:** 2026-04-23
**Status:** Approved
**Depends on:** none (isolated UI tweak)
**Reference:** `client/src/components/common/profile/ChatExtra.tsx:76` — `const SHOW_USERNAME = false;` + `{SHOW_USERNAME && activeUsernames && renderUsernames(activeUsernames)}` at line 432

---

## 1. Goal

Hide the `@username` row (and related controls) in the USER profile info page. Group / channel invite-link row is unaffected.

**Success criteria:**
1. Opening any user's profile info page shows no `@username` row.
2. QR button and secondary-username sub-links tied to the `@username` row are also hidden.
3. Other user info — name, bio, phone, common groups, personal channel, status — renders unchanged.
4. Group / channel profile info pages still show their public `t.me/<link>` invite-link row.
5. Chat top-bar username display, in-message @mention links, forward attribution — all unchanged (out of scope).
6. No runtime cost: compile-time elimination.

**Non-goals:**
- Hiding phone numbers (client hides these too via `SHOW_PHONE_NUMBER = false`; tdesktop phone hiding is NOT part of this task and can be done later if wanted).
- Hiding @mention clickability in messages.
- Hiding the peer's username anywhere else in the app (share targets, top bar, etc.).
- `TDESKTOP_EMPLOYEE_MODE` gating — this is a fork-wide default matching the client behavior.

---

## 2. Approach

**Hardcoded compile-time constant + `if constexpr` guard**, mirroring the Web client's `const SHOW_USERNAME = false;` pattern 1:1.

### Component

File: `Telegram/SourceFiles/info/profile/info_profile_actions.cpp`

Add to the anonymous namespace near the top (where other file-local constants already live):

```cpp
// Hide @username row (and its QR button / secondary usernames) in user profile.
// Matches Web client's SHOW_USERNAME = false in
// client/src/components/common/profile/ChatExtra.tsx.
// Group/channel invite-link row is NOT affected.
constexpr auto kShowUserUsername = false;
```

Then wrap the existing `usernameLine` construction block (roughly lines 1677–1710, which includes the `addInfoOneLine` call, `callback` assignment, `overrideLinkClickHandler`, `setContextMenuHook`, the QR button creation, its visibility rpl subscription, its click handler) with:

```cpp
if constexpr (kShowUserUsername) {
    const auto usernameLine = addInfoOneLine(
        UsernamesSubtext(_peer, tr::lng_info_username_label()),
        UsernameValue(user, true) | rpl::map(...),
        QString(),
        st::infoProfileLabeledUsernamePadding);
    // ... entire existing username-related block ...
}
```

No code inside the block changes. Only the surrounding `if constexpr` wrapper + the file-scope constant are new.

### Integration point

Only ONE insertion point — `ActionsFiller::fillUserActions` or whichever function constructs the user-profile action list. Located in `info_profile_actions.cpp` around line 1677.

---

## 3. Data Flow

Compile-time: `kShowUserUsername` is `false`, so `if constexpr` branches false and the compiler eliminates the entire username block. No runtime check, no dead-code warning (C++17 `if constexpr` discards the untaken branch).

Run-time: user profile page is built. The username-related widgets are never created. The layout flows naturally without them (existing `addInfoOneLine`/widget chain handles omitted rows).

---

## 4. What Is Not Changed

- `UsernameValue` / `UsernamesValue` / `PlainUsernameValue` / `UsernamesSubtext` helpers in `info_profile_values.{h,cpp}` — retained. They serve other surfaces (e.g., mentions, search, peer URL resolution).
- Channel / group info page's invite-link row (t.me/...) — different construction branch, unaffected.
- Chat top bar rendering, in-message mention links, forward attribution, autocompletion popup — all separate code paths, unchanged.
- Self profile (settings main page) — a different screen, untouched by this change.

---

## 5. Edge Cases

| Scenario | Behavior |
|---|---|
| User has no username | The block was already conditional (empty string skip); with `if constexpr false` it never runs — no functional difference. |
| User is a bot | Bot profile uses the same `fillUserActions` branch; `@username` row also hidden. Bot's other info (description, commands, etc.) is in separate blocks and unaffected. |
| User is self | Self viewing own profile in info page — `@username` hidden, consistent with client behavior. Settings/main page shows self's username via a different screen (out of scope). |
| Group / channel profile | Different branch in `info_profile_actions.cpp` — t.me invite-link row renders normally. |
| Peer has multiple usernames (primary + alternates) | The sub-link list of alternates is part of the wrapped block — hidden. |
| QR button adjacent to username row | Part of the wrapped block — hidden. |

---

## 6. Testing (smoke)

1. Open a contact's profile → no `@username` row, no QR button there. Name / phone / bio / common groups visible.
2. Open a non-contact user's profile (from a group member list) → same.
3. Open a bot's profile (e.g., @BotFather) → no `@username` row; bot description/commands visible.
4. Open self's profile via info page (not via Settings main) → no `@username` row.
5. Open a public channel's info → t.me invite-link row **present** (regression check).
6. Open a public group's info → t.me invite-link row **present**.
7. Tap an @mention in a message → jumps to that user's profile (which has no username row) — link itself still works.

---

## 7. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Upstream refactors the wrapped block (splits usernameLine creation from QR button setup) | Low | Wrapping is contiguous lines; merge conflicts obvious and trivial to hand-resolve. |
| `if constexpr` rejects a block containing types only defined inside (e.g., captures)| None | The block uses only previously-declared types/variables; no odd dependencies. |
| Layout visual artifact (gap where username row used to be) | Low | The `addInfoOneLine` call is what adds the row container; skipping the call means no container is created, so no gap. |
| Future request to toggle at runtime | None | Changing `constexpr` to a setting is a small future refactor; not speculative now. |

---

## 8. Implementation Ordering

Single-task plan. One file edit. Steps:
1. Grep + read the exact block in `info_profile_actions.cpp` to confirm line range.
2. Add the file-scope `constexpr` constant.
3. Wrap the block with `if constexpr (kShowUserUsername) { ... }`.
4. Compile. Smoke-test the 7 items above.
5. Tag `hide-user-username-v1` + push.
