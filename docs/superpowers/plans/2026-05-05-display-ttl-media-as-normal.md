# Display TTL Photos and Videos as Normal Media — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make TTL'd photos and TTL'd regular videos render as normal media in the desktop client (no countdown, no "secret media" service text), preserving voice and round-video TTL countdown UI.

**Architecture:** Two coordinated edits flip the routing of self-destructing media at parse time. `CheckMessageMedia` no longer classifies TTL'd photos / regular videos as `HasUnsupportedTimeToLive`, so HistoryItem's ctor goes down the normal media path instead of `createServiceFromMtp`. `CreateMedia` then drops its defensive bail-outs and, for documents only, conditionally suppresses `ttlSeconds` so TTL'd regular videos don't trigger the renderer's countdown UI while voice/round still do.

**Tech Stack:** C++20, Qt 6, tdesktop's HistoryItem / Data::Media subsystem. Build via CMake + MSVC.

**Spec:** `docs/superpowers/specs/2026-05-05-display-ttl-media-as-normal-design.md`

---

## File Map

- **Modify:** `Telegram/SourceFiles/history/history_item_helpers.cpp` — `CheckMessageMedia` photo + document branches (lines 953-980)
- **Modify:** `Telegram/SourceFiles/history/history_item.cpp` — `CreateMedia` photo + document branches (lines 273-324)

No new files. No test files (tdesktop has no unit tests covering `CheckMessageMedia`; verification is build + manual smoke per Testing Plan).

---

## Task 1: Apply the four source edits

All four edits ship together in a single commit — the behavior is incoherent if only some land. Each edit is mechanical: delete the TTL early-return that routes to "secret media" service-message land, plus (for the document branch in `CreateMedia`) one ternary conditional so video TTL drops to zero while voice/round preserve it.

**Files:**
- Modify: `/mnt/d/ProjectKaka/TelegramProClient/tdesktop/Telegram/SourceFiles/history/history_item_helpers.cpp` (lines 953-980)
- Modify: `/mnt/d/ProjectKaka/TelegramProClient/tdesktop/Telegram/SourceFiles/history/history_item.cpp` (lines 273-324)

- [ ] **Step 1: Edit `CheckMessageMedia` photo branch**

Replace `old_string`:

```cpp
	}, [](const MTPDmessageMediaPhoto &data) {
		const auto photo = data.vphoto();
		if (data.vttl_seconds()) {
			return Result::HasUnsupportedTimeToLive;
		} else if (!photo) {
			return Result::Empty;
		}
		return photo->match([](const MTPDphoto &) {
			return Result::Good;
		}, [](const MTPDphotoEmpty &) {
			return Result::Empty;
		});
```

with `new_string`:

```cpp
	}, [](const MTPDmessageMediaPhoto &data) {
		const auto photo = data.vphoto();
		if (!photo) {
			return Result::Empty;
		}
		return photo->match([](const MTPDphoto &) {
			return Result::Good;
		}, [](const MTPDphotoEmpty &) {
			return Result::Empty;
		});
```

The TTL early-return is deleted; everything else identical.

- [ ] **Step 2: Edit `CheckMessageMedia` document branch**

Replace `old_string`:

```cpp
	}, [](const MTPDmessageMediaDocument &data) {
		const auto document = data.vdocument();
		if (data.vttl_seconds()) {
			if (data.is_video()) {
				return Result::HasUnsupportedTimeToLive;
			} else if (!document) {
				return Result::HasExpiredMediaTimeToLive;
			}
		} else if (!document) {
			return Result::Empty;
		}
		return document->match([](const MTPDdocument &) {
			return Result::Good;
		}, [](const MTPDdocumentEmpty &) {
			return Result::Empty;
		});
```

with `new_string`:

```cpp
	}, [](const MTPDmessageMediaDocument &data) {
		const auto document = data.vdocument();
		if (!document) {
			return data.vttl_seconds()
				? Result::HasExpiredMediaTimeToLive
				: Result::Empty;
		}
		return document->match([](const MTPDdocument &) {
			return Result::Good;
		}, [](const MTPDdocumentEmpty &) {
			return Result::Empty;
		});
```

The `is_video()` arm is gone; the no-document case is unified into a single ternary that still routes to `HasExpiredMediaTimeToLive` when TTL is set but no document data arrived.

- [ ] **Step 3: Edit `CreateMedia` photo branch**

Replace `old_string`:

```cpp
	}, [&](const MTPDmessageMediaPhoto &media) -> Result {
		const auto photo = media.vphoto();
		if (media.vttl_seconds()) {
			LOG(("App Error: "
				"Unexpected MTPMessageMediaPhoto "
				"with ttl_seconds in CreateMedia."));
			return nullptr;
		} else if (!photo) {
			LOG(("API Error: "
				"Got MTPMessageMediaPhoto "
				"without photo and without ttl_seconds."));
			return nullptr;
		}
		return photo->match([&](const MTPDphoto &photo) -> Result {
			return std::make_unique<Data::MediaPhoto>(
				item,
				item->history()->owner().processPhoto(photo),
				media.is_spoiler());
		}, [](const MTPDphotoEmpty &) -> Result {
			return nullptr;
		});
```

with `new_string`:

```cpp
	}, [&](const MTPDmessageMediaPhoto &media) -> Result {
		const auto photo = media.vphoto();
		if (!photo) {
			LOG(("API Error: "
				"Got MTPMessageMediaPhoto without photo."));
			return nullptr;
		}
		return photo->match([&](const MTPDphoto &photo) -> Result {
			return std::make_unique<Data::MediaPhoto>(
				item,
				item->history()->owner().processPhoto(photo),
				media.is_spoiler());
		}, [](const MTPDphotoEmpty &) -> Result {
			return nullptr;
		});
```

TTL early-return deleted; the remaining `!photo` log message loses its now-misleading "without ttl_seconds" qualifier.

- [ ] **Step 4: Edit `CreateMedia` document branch**

Replace `old_string`:

```cpp
	}, [&](const MTPDmessageMediaDocument &media) -> Result {
		const auto document = media.vdocument();
		if (media.vttl_seconds() && media.is_video()) {
			LOG(("App Error: "
				"Unexpected MTPMessageMediaDocument "
				"with ttl_seconds in CreateMedia."));
			return nullptr;
		} else if (!document) {
			LOG(("API Error: "
				"Got MTPMessageMediaDocument "
				"without document and without ttl_seconds."));
			return nullptr;
		}
		return document->match([&](const MTPDdocument &document) -> Result {
			const auto list = media.valt_documents();
			const auto owner = &item->history()->owner();
			const auto data = owner->processDocument(document, list);
			using Args = Data::MediaFile::Args;
			return std::make_unique<Data::MediaFile>(item, data, Args{
				.ttlSeconds = media.vttl_seconds().value_or_empty(),
				.videoCover = (media.vvideo_cover()
					? owner->processPhoto(*media.vvideo_cover()).get()
					: nullptr),
				.videoTimestamp = media.vvideo_timestamp().value_or_empty(),
				.hasQualitiesList = list && !list->v.isEmpty(),
				.skipPremiumEffect = media.is_nopremium(),
				.spoiler = media.is_spoiler(),
			});
		}, [](const MTPDdocumentEmpty &) -> Result {
			return nullptr;
		});
```

with `new_string`:

```cpp
	}, [&](const MTPDmessageMediaDocument &media) -> Result {
		const auto document = media.vdocument();
		if (!document) {
			LOG(("API Error: "
				"Got MTPMessageMediaDocument without document."));
			return nullptr;
		}
		return document->match([&](const MTPDdocument &document) -> Result {
			const auto list = media.valt_documents();
			const auto owner = &item->history()->owner();
			const auto data = owner->processDocument(document, list);
			using Args = Data::MediaFile::Args;
			return std::make_unique<Data::MediaFile>(item, data, Args{
				.ttlSeconds = media.is_video()
					? 0
					: media.vttl_seconds().value_or_empty(),
				.videoCover = (media.vvideo_cover()
					? owner->processPhoto(*media.vvideo_cover()).get()
					: nullptr),
				.videoTimestamp = media.vvideo_timestamp().value_or_empty(),
				.hasQualitiesList = list && !list->v.isEmpty(),
				.skipPremiumEffect = media.is_nopremium(),
				.spoiler = media.is_spoiler(),
			});
		}, [](const MTPDdocumentEmpty &) -> Result {
			return nullptr;
		});
```

Video+TTL early-return deleted; `!document` log shortened; `.ttlSeconds` becomes `media.is_video() ? 0 : media.vttl_seconds().value_or_empty()` so regular video TTL drops the timer while voice/round preserves it.

- [ ] **Step 5: Touch the modified files so MSBuild on WSL picks up the change**

Run from `/mnt/d/ProjectKaka/TelegramProClient/tdesktop`:

```bash
touch \
  Telegram/SourceFiles/history/history_item_helpers.cpp \
  Telegram/SourceFiles/history/history_item.cpp
```

Expected: no output. (WSL on `/mnt/d` sometimes skips mtime updates after the Edit tool — explicit `touch` ensures MSBuild detects the change. Established convention from prior fork commits.)

- [ ] **Step 6: Inspect the diff to confirm exactly four sites changed and nothing else**

Run from `/mnt/d/ProjectKaka/TelegramProClient/tdesktop`:

```bash
git diff --stat Telegram/SourceFiles/history/history_item_helpers.cpp Telegram/SourceFiles/history/history_item.cpp
git diff Telegram/SourceFiles/history/history_item_helpers.cpp Telegram/SourceFiles/history/history_item.cpp | head -120
```

Expected: two files modified, exactly two hunks per file (photo branch + document branch). No changes outside the listed line ranges. The diff should show the deletions described in Steps 1–4 and no additions of new functions, includes, or unrelated formatting.

If anything else changed, abort and reread the spec — do not proceed.

- [ ] **Step 7: Commit**

Run from `/mnt/d/ProjectKaka/TelegramProClient/tdesktop`:

```bash
git add \
  Telegram/SourceFiles/history/history_item_helpers.cpp \
  Telegram/SourceFiles/history/history_item.cpp
git commit -m "$(cat <<'EOF'
feat(media): render TTL photos and videos as normal media

Mirror the TT (telegram-tt) custom-fork behavior of dropping the
self-destruct routing for ttl_seconds-flagged photos and regular
videos. Voice and round-video TTL still flow through the existing
countdown UI; already-expired media (no document data) still goes
to the lng_ttl_*_expired service text.

Two coordinated changes are required because tdesktop routes via
two cooperating choke points:

- CheckMessageMedia: TTL'd photos/videos with valid payload now
  return Good instead of HasUnsupportedTimeToLive.
- CreateMedia: defensive early-returns deleted; for documents,
  ttlSeconds is suppressed when is_video() so the renderer draws
  a normal video bubble (voice/round preserve ttl for countdown).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

Expected: `[customization <hash>] feat(media): render TTL photos and videos as normal media` with two files changed.

- [ ] **Step 8: Tag the commit for traceability with prior fork tags**

Run from `/mnt/d/ProjectKaka/TelegramProClient/tdesktop`:

```bash
git tag display-ttl-media-as-normal-v1
```

Expected: silent success. Tag is local-only at this stage; pushed alongside the next release.

---

## Task 2: Build and manual smoke verification

The change is too small to add automated tests for and the surrounding code path has no existing unit-test scaffolding. Verification is via Release build + the spec's manual scenarios.

**Files:** none modified.

- [ ] **Step 1: Build Release**

User runs locally on Windows (WSL cannot drive MSBuild here). From a `x64 Native Tools Command Prompt`:

```
cmake --build out --config Release --target Telegram
```

Expected: `Telegram.exe` rebuilds at `out/Release/Telegram.exe`. If the build fails with a PDB or "file in use" error, close any running `Telegram.exe` and retry — do **not** delete files or skip hooks.

- [ ] **Step 2: Smoke test — TTL photo received renders as normal photo**

With a buddy account on mobile, send a self-destruct photo (any TTL) to the test employee account. On the rebuilt desktop client:

- The bubble should show the photo inline like a normal photo.
- No countdown timer, no fire icon, no "secret photo" service text.
- Click should open the fullscreen viewer like any other photo.

Pass = bubble looks identical to a normal photo. Fail = countdown or service-text shown → re-check Steps 1 & 3 of Task 1.

- [ ] **Step 3: Smoke test — TTL video received renders as normal video**

Buddy sends a self-destruct video (any TTL). Desktop should show an inline video player with normal play / pause; no countdown.

Pass = behaves like a normal video. Fail = countdown shown → re-check the `media.is_video() ? 0 : ...` ternary in Step 4 of Task 1.

- [ ] **Step 4: Regression — TTL voice keeps countdown**

Buddy sends a self-destruct voice message. Desktop should still show the countdown stopwatch and "play once" behavior.

Pass = countdown UI visible. Fail = voice now shows as normal audio with no timer → the ternary's else-branch is wrong; voice should preserve `vttl_seconds().value_or_empty()`.

- [ ] **Step 5: Regression — TTL round video keeps countdown**

Buddy sends a self-destruct round video. Desktop should still show the countdown overlay and "play once" behavior.

Pass = countdown UI visible. Fail = round video shows as normal video with no timer → check that `media.is_video()` is false for round videos (the wrapper-level flag, not the document attribute).

- [ ] **Step 6: Regression — already-expired TTL still shows expired service text**

Trigger an expired-media path (most reliable: open chat history that contains a TTL message previously expired server-side; alternatively, ask buddy to send and immediately delete a TTL photo before desktop syncs). Desktop should render the upstream `lng_ttl_photo_expired` (or `_video_/_voice_/_round_expired`) service text.

Pass = "TTL photo expired" service text. Fail = bubble missing or crashes → check that the `!document` ternary in Step 2 of Task 1 still routes to `HasExpiredMediaTimeToLive` when `vttl_seconds()` is set.

- [ ] **Step 7: Sender-deletes-after-view behavior unchanged**

Buddy sends TTL photo, employee opens chat (photo renders normally per Step 2), buddy then deletes the message. The bubble on desktop should disappear via the standard delete-message flow within a few seconds.

Pass = bubble gone. Fail = stale bubble persists → out of scope for this plan; capture as a separate issue.

- [ ] **Step 8: Report results**

Confirm Steps 2–7 results to the user. If all pass, the feature is verified and ready for the next release-publishing cycle. The feature ships in the next version bump (separate plan / commit).

---

## Notes for the executor

- This plan is intentionally a single feature commit with no decomposition — all four edits must be in one revision because shipping any subset leaves the codebase incoherent (e.g., applying only `CheckMessageMedia` without `CreateMedia` causes assertions to log at runtime).
- Do **not** add new lang strings, new include directives, or new helper functions. The change is purely deletion + a one-line ternary.
- Do **not** modify `prepareServiceTextForMessage`, `setSelfDestruct`, or any rendering-layer file. The spec specifically scopes those out.
- Do **not** touch the send-side TTL UI. Employees should not be sending TTL media, but disabling the send path is a different feature with its own design.
- File encoding stays UTF-8 without BOM with CRLF line endings (per `tdesktop/AGENTS.md`). The Edit tool preserves these by default; do not introduce LF or BOM.
- After Task 1 commits, do not push to origin. The tag and commit ride along with the next release-publishing cycle (per the runbook in `docs/superpowers/runbooks/release-publishing.md`).
