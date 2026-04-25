# Hide Phone Number Row in User Profile Design Spec

**Date:** 2026-04-25
**Status:** Approved
**Depends on:** none (isolated UI tweak; mirrors `hide-user-username-v1`)
**Reference:** `client/src/components/common/profile/ChatExtra.tsx:79` — `const SHOW_PHONE_NUMBER = false;` + `{SHOW_PHONE_NUMBER && Boolean(formattedNumber?.length) && (...)}` at line 418

---

## 1. Goal

Hide the phone-number row (and its copy / "add to contacts" context-menu hooks) in the USER profile info page. All other surfaces that render phone numbers — shared-contact message bubble, short-info popup, edit-contact dialog, self phone in Settings — are unaffected.

**Success criteria:**
1. Opening any user's profile info page shows no "Mobile" row.
2. The right-click "Copy phone" / "Add phone to contacts" menu items tied to that row are unreachable (the row that hosts them is gone).
3. Other user info — name, bio, common groups, personal channel, status — renders unchanged.
4. Group / channel profile info pages are untouched (they never render a phone row anyway).
5. Settings → main page (self profile) still shows the user's own phone number.
6. No runtime cost: compile-time elimination via `if constexpr`.

**Non-goals:**
- Hiding the phone number on the shared-contact message bubble (`history_view_contact.cpp`).
- Hiding the phone in the short-info popup (`prepare_short_info_box.cpp`).
- Hiding the phone in the edit-contact dialog (`edit_contact_box.cpp`).
- Hiding the user's own phone in Settings main / sidebar.
- Disabling the phone-click handler (`phone_click_handler.cpp`) elsewhere in the app.
- Touching `PhoneValue` / `PhoneOrHiddenValue` data accessors — UI rendering only.
- `TDESKTOP_EMPLOYEE_MODE` gating — this is a fork-wide default matching the Web client behavior.

---

## 2. Approach

**Hardcoded compile-time constant + `if constexpr` guard**, mirroring both the Web client's `const SHOW_PHONE_NUMBER = false;` and the existing `hide-user-username-v1` pattern in this same file.

### Component

File: `Telegram/SourceFiles/info/profile/info_profile_actions.cpp`

Add to the anonymous namespace near the top, alongside the existing `kShowUserUsername` constant:

```cpp
// Hide phone-number row (and its copy / add-contact context-menu hooks)
// in user profile. Matches Web client's SHOW_PHONE_NUMBER = false in
// client/src/components/common/profile/ChatExtra.tsx.
// Self phone in Settings, shared-contact message bubble, short-info popup,
// and edit-contact dialog are NOT affected.
constexpr auto kShowUserPhone = false;
```

Then wrap the existing phone block (lines ~1653–1675, the entire scope opened by the lone `{` at line 1653) with:

```cpp
if constexpr (kShowUserPhone) {
    const auto phoneLabel = addInfoOneLine(
        tr::lng_info_mobile_label(),
        PhoneOrHiddenValue(user),
        tr::lng_profile_copy_phone(tr::now)).text;
    // ... entire existing context-menu hook + AddPhoneMenu wiring ...
}
```

No code inside the block changes. Only the surrounding `if constexpr` wrapper + the file-scope constant are new.

### Integration point

Single insertion point in `DetailsFiller::setupInfo()` — the `if (const auto user = _peer->asUser())` branch, just before the bio/about block and the existing `if constexpr (kShowUserUsername)` block.

---

## 3. Data Flow

Compile-time: `kShowUserPhone == false` → `if constexpr` discards the block entirely; no widget construction, no rpl subscription, no menu registration.

Run-time: profile info layout is built. The phone row container is never created. Subsequent rows (bio, optional username block, etc.) flow up to fill the space; existing `addInfoOneLine` / vertical-layout chain handles omitted rows naturally — same way the username block already does after `hide-user-username-v1`.

---

## 4. What Is Not Changed

- `PhoneValue` / `PhoneOrHiddenValue` in `info/profile/info_profile_values.{h,cpp}` — retained. Used by `boxes/url_auth_box.cpp`, `settings/sections/settings_main.cpp`, and Passport / Payments flows.
- `Ui::FormatPhone` and the phone-click handler — untouched. Phone-formatted strings still render anywhere else they're embedded (shared contact bubbles, short-info popups, etc.).
- `AddPhoneMenu` itself — function remains; it's just no longer wired up at this one call site.
- `UserData::phone()` / API layer / serialization — completely untouched.
- Group / channel profile info — different branch, never had a phone row.
- Self profile in Settings — a separate screen, hits `Info::Profile::PhoneValue` directly, not gated by this constant.

---

## 5. Edge Cases

| Scenario | Behavior |
|---|---|
| User has no phone (phone is empty / hidden) | The row was already showing "Hidden" placeholder via `PhoneOrHiddenValue`. With `if constexpr false` the row is gone entirely — strictly more hidden, no UX regression. |
| User is a bot | Bots typically have empty phone; same as above. The `AddPhoneMenu` for "Add to contacts" was meaningless on bots and is gone. |
| User is self viewing own profile via Info | Phone row hidden in Info side panel. Self phone is still visible in Settings main page (different screen). Aligns with Web client behavior. |
| Right-click on the (now absent) phone row | N/A — the row doesn't exist, so no context menu can target it. Right-click on adjacent rows (name, bio) keeps their own context menus working as before. |
| Group / channel profile | Different branch — never reached. No regression possible. |
| User opens "Add to contacts" via the existing left-side menu / "+" button | Unaffected — that flow is in `add_contact_box.cpp`, not gated by this constant. The user can still be added manually. |
| Receiving a contact-card message in chat | The phone shows on the message bubble (`history_view_contact.cpp`) — unchanged, by design. |

---

## 6. Testing (smoke)

1. Open a contact's profile → no "Mobile" row. Name / bio / common groups / personal channel still visible.
2. Open a non-contact user's profile (e.g., from a group member list) → same.
3. Open a bot's profile (e.g., `@BotFather`) → no Mobile row; bot description / commands visible.
4. Open self profile via Info side panel → no Mobile row.
5. Open Settings → main page → own phone number **present** (regression check).
6. Send / receive a contact-card message → phone visible on the bubble (regression check).
7. Right-click on the user's name row in the (now phoneless) profile → name's own context menu still works.
8. Open a public channel's info → t.me invite-link row present (untouched, regression check).
9. Confirm `hide-user-username-v1` still works — `@username` row remains hidden (regression check on adjacent constant).

---

## 7. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Upstream refactors split the phone block (e.g., moves `AddPhoneMenu` to a new function) | Low | Wrapping is contiguous lines (~22 lines, single `{}` scope). Merge conflicts obvious; reapply by re-wrapping the new block. |
| Layout visual gap where phone row used to be | Low | Same as username case — `addInfoOneLine` is what creates the row; skipping the call means no container, no gap. Visually verified with username work. |
| `AddPhoneMenu` referenced elsewhere by include | None | Function definition stays in `info_profile_phone_menu.cpp`; only its call at line 1672 is removed via `if constexpr false`. |
| Future request to toggle at runtime per session / admin policy | None | Promoting `constexpr` to a runtime flag is a tiny future refactor; not speculative now. |
| Conflict with `hide-user-username-v1` constant | None | Independent named constants in the same anonymous namespace, independent `if constexpr` blocks. |

---

## 8. Implementation Ordering

Single-task plan. One file edit. Steps:
1. Grep + read the exact phone block in `info_profile_actions.cpp:1653-1675` to confirm line range and brace nesting.
2. Add the file-scope `constexpr auto kShowUserPhone = false;` constant in the anonymous namespace, immediately after `kShowUserUsername`.
3. Wrap the phone block with `if constexpr (kShowUserPhone) { ... }`. Re-indent inner lines one tab deeper.
4. Compile (Debug). `cmake --build out --config Debug --target Telegram`.
5. Smoke-test the 9 items above.
6. Commit on `customization` branch with message `feat(hide): hide phone row in user profile`.
7. Tag `hide-user-phone-v1`. Do not push (user pushes batches manually).
