# Hide Privacy & Security Row in Main Settings Menu Design Spec

**Date:** 2026-04-30
**Status:** Approved
**Depends on:** none (isolated UI tweak; mirrors `hide-user-username-v1` and `hide-user-phone-v1`)
**Reference:** Anti-leak / employee-controlled fork theme; UI-only — does NOT touch deep-link handlers, MTProto API, or sub-section parent registrations.

---

## 1. Goal

Hide the "Privacy and Security" row in the main Settings menu, so employees cannot navigate to the Privacy section by clicking through Settings. Search-in-settings keyword matches that target this row are also suppressed (because the keywords live inside the wrapped block).

**Success criteria:**
1. Opening Settings → main menu shows no "Privacy and Security" row, no lock icon between "Notifications" and "Chat Settings".
2. Settings search for "security", "passcode", "password", or "2fa" returns no main-menu hit (these keywords were declared on the now-hidden button).
3. All other Settings rows (Notifications, Chat Settings, Folders, Advanced, etc.) render unchanged.
4. The Privacy section's child pages (Active Sessions, Blocked Peers, Global TTL, Passkeys, Websites) and their `parentId = PrivacySecurityId()` declarations remain intact — they rely on the parent ID for lookup, not on the parent's menu visibility.
5. No runtime cost: compile-time elimination via `if constexpr`.

**Non-goals:**
- Blocking deep-link entry points (`tg://settings/privacy_and_security` etc.) handled in `core/deep_links/deep_links_settings.cpp`.
- Disabling the "Open Settings" button in the suggest-archive-and-mute popup (`window_session_controller.cpp:1820`).
- Hiding any Privacy sub-pages directly (Active Sessions, Blocked Peers, etc.) — they have their own routes; out of scope.
- Touching `PrivacySecurityId()`, `BuildPrivacySecuritySectionContent()`, or the `kPrivacySecuritySection` registration.
- Disabling backend privacy MTProto calls.
- `TDESKTOP_EMPLOYEE_MODE` gating — fork-wide default, matching `kShowUserUsername` / `kShowUserPhone` precedent.

---

## 2. Approach

**Hardcoded compile-time constant + `if constexpr` guard**, mirroring the previous `hide-user-username-v1` / `hide-user-phone-v1` pattern.

### Component

File: `Telegram/SourceFiles/settings/sections/settings_main.cpp`

Add to the anonymous namespace (begins at line 91), immediately after the existing `constexpr auto kSugValidatePhone` (line 95):

```cpp
// Hide "Privacy and Security" row in main Settings menu. Force-hides
// the entry; deep-link routes (tg://settings/privacy_and_security) and
// programmatic showSettings(PrivacySecurityId()) calls (e.g., the
// suggest-archive-and-mute popup) still work — by design, this is UI-only.
// Sub-pages (Active Sessions, Blocked Peers, etc.) still register their
// parentId = PrivacySecurityId() — that's a lookup ID, unaffected.
constexpr auto kShowSettingsPrivacy = false;
```

Then wrap the existing 6-line `addSectionButton` block at lines 384-389:

```cpp
builder.addSectionButton({
    .title = tr::lng_settings_section_privacy(),
    .targetSection = PrivacySecurityId(),
    .icon = { &st::menuIconLock },
    .keywords = { u"security"_q, u"passcode"_q, u"password"_q, u"2fa"_q },
});
```

with:

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

No code inside the block changes. Only the surrounding `if constexpr` wrapper + the file-scope constant are new.

### Integration point

Single insertion point — the function that builds the main Settings menu rows in `settings_main.cpp`. Located between the "Notifications" row (line 377-382) and the "Chat Settings" row (line 391-396).

---

## 3. Data Flow

Compile-time: `kShowSettingsPrivacy` is `false`, so `if constexpr` discards the `addSectionButton` call entirely. No widget added to the layout, no keywords registered with the search index.

Run-time: Settings main menu is built. The Privacy row container is never created; layout flows naturally with adjacent rows ("Notifications" directly above, "Chat Settings" directly below) — same flow handling as `hide-user-username-v1` / `hide-user-phone-v1` after their wraps.

Deep-link / programmatic navigation paths (`showSettings(PrivacySecurityId())`) continue to work because they look up the section by ID in the registry — independent of whether the main menu has a row pointing at it.

---

## 4. What Is Not Changed

- `Telegram/SourceFiles/settings/sections/settings_privacy_security.{cpp,h}` — Privacy section itself is fully intact.
- `Telegram/SourceFiles/core/deep_links/deep_links_settings.cpp` — deep-link handlers for `tg://settings/privacy*` URLs untouched.
- `Telegram/SourceFiles/core/local_url_handlers.cpp` — URL handling untouched.
- `Telegram/SourceFiles/window/window_session_controller.cpp:1820` — the "Open Settings" button in the suggest-archive-and-mute popup still navigates to Privacy.
- `Telegram/SourceFiles/settings/sections/settings_active_sessions.cpp:1314`, `settings_blocked_peers.cpp:337`, `settings_global_ttl.cpp:554`, `settings_passkeys.cpp:134`, `settings_websites.cpp:849` — sub-section `parentId = PrivacySecurityId()` registrations untouched.
- All MTProto privacy-related API calls — untouched.

---

## 5. Edge Cases

| Scenario | Behavior |
|---|---|
| Employee opens Settings → main menu | No "Privacy and Security" row visible. Other rows render in original order minus this one. |
| Employee uses Settings search bar with "security" / "passcode" / "password" / "2fa" | No results from the main Privacy menu (keywords were attached to the hidden button). Other settings that include these terms in their own keywords/text may still match. |
| Employee clicks a notification toast / button that navigates via `showSettings(PrivacySecurityId())` | Privacy section opens normally. UI-only hide does not interrupt programmatic flow. |
| Employee enters `tg://settings/privacy_and_security` (or sub-routes) in the address bar / via Telegram link | Privacy section opens normally. Deep-link handlers unmodified. |
| Employee navigates to a Privacy sub-page (Active Sessions, Blocked Peers, etc.) via its own deep link | Sub-page opens, parent registration intact. |
| Future user wants to re-enable | Flip `kShowSettingsPrivacy = true` and recompile. |
| Adjacent feature (`hide-user-username-v1` etc.) | Independent; lives in a different file (`info_profile_actions.cpp`). No interaction. |

---

## 6. Testing (smoke)

1. Open Settings → main menu → confirm no "Privacy and Security" row, no lock icon between "Notifications" and "Chat Settings".
2. Use Settings search bar → type "security" → no main-menu hit pointing to Privacy.
3. Use Settings search bar → type "passcode" → no main-menu hit.
4. Use Settings search bar → type "2fa" → no main-menu hit.
5. Navigate to Settings → Notifications → Chat Settings → Folders → Advanced → all visible and functional (regression check).
6. (Optional, if a test account triggers it) Trigger the "suggest-archive-and-mute" popup → click "Open Settings" → Privacy section opens correctly (verifies UI-only scope).
7. Manually paste `tg://settings/privacy_and_security` into a chat, click the link → Privacy section opens (regression check on deep links).
8. Verify `hide-user-username-v1` and `hide-user-phone-v1` still work (open any user profile, no @username row, no Mobile row) — adjacent-feature regression.

---

## 7. Risks

| Risk | Likelihood | Mitigation |
|---|---|---|
| Upstream renames `addSectionButton` or the menu-build function | Low | Wrap is a contiguous 6-line block; merge conflict obvious and trivial. |
| Upstream splits the block into multiple statements | Low | Same as above — re-wrap the new block. |
| Settings search index expects every keyword to be registered (causes crash on empty index) | None | The search index aggregates keywords across all `addSectionButton` calls; skipping one entry is exactly how upstream behaves when a feature is disabled by config. |
| Future request to also block deep links | None | A small future change in `deep_links_settings.cpp` if needed; not speculative now. |
| `if constexpr` rejects discarded branch types | None | Block uses only previously-declared types/builder API; no template tricks. |
| Conflict with `kSugValidatePhone` constant | None | Independent named constant in same anonymous namespace. |

---

## 8. Implementation Ordering

Single-task plan. One file edit, two hunks. Steps:
1. Grep + read the exact block in `settings_main.cpp:384-389` to confirm line range.
2. Add the file-scope `constexpr auto kShowSettingsPrivacy = false;` constant in the anonymous namespace, immediately after `kSugValidatePhone`.
3. Wrap the 6-line `addSectionButton` block with `if constexpr (kShowSettingsPrivacy) { ... }`. Re-indent inner lines one tab deeper.
4. `touch` the file (WSL/9P mtime defense).
5. Self-review: `git diff --stat` shows only this one file; per-file `kShowSettingsPrivacy` count is 2 (constant declaration + the `if constexpr` site).
6. Compile (Debug). `cmake --build out --config Debug --target Telegram`.
7. Smoke-test items 1-8 above.
8. Commit on `customization` branch with message `feat(hide): hide Privacy & Security row in main Settings menu`.
9. Tag `hide-settings-privacy-v1`. Do not push (user pushes batches manually).
