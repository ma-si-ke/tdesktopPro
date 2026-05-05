# Display TTL Photos and Videos as Normal Media — Design

**Date:** 2026-05-05
**Branch:** customization
**Source of inspiration:** TT (telegram-tt) custom fork in `client/`, single-file change in
`src/api/gramjs/apiBuilders/messageContent.ts` removing the `if (ttlSeconds !== undefined) return undefined;` early-return.

## Goal

Render incoming self-destructing photos and self-destructing videos (Telegram's
"阅后即焚" feature, i.e. `messageMediaPhoto` / `messageMediaDocument` carrying a
`ttl_seconds` flag) as plain photos / videos in the desktop client — no
countdown, no "secret media" service-message text, no visual marker indicating
the media was originally TTL-bound.

Voice messages and round videos with TTL keep their existing countdown UI.

## Why

The fork is for internal employees who must be able to see all media a
counterparty sends them, including media the counterparty intended to be
ephemeral. Upstream tdesktop hides such media behind a service message
("X sent you a secret photo") with no inline preview because the secret-photo
viewing UI exists only on mobile. Mirroring TT's behavior gives parity:
desktop users see the same content their counterparty believes is private.

## Scope

**In scope (will display as normal media, no marker):**

- `MTPDmessageMediaPhoto` with `ttl_seconds` set
- `MTPDmessageMediaDocument` with `ttl_seconds` set AND wrapper flag `is_video()`
  (i.e. regular video TTL media)

**Out of scope (no behavior change):**

- TTL voice messages — keep countdown UI
- TTL round videos — keep countdown UI
- Already-expired TTL media (server returned no document/photo data) — keep
  current "TTL X expired" service-message rendering
- Sender side — no change; the customized client doesn't expose new send
  controls and existing send-TTL UI remains intact
- Spoiler flag (`is_spoiler`) — independent of TTL; preserved as-is
- Server-side "viewed" acknowledgment — desktop never triggered this in the
  first place (it required the mobile stopwatch UI), so behavior unchanged
- Sender-initiated deletion — handled by the existing message-deletion path,
  the same as for normal media

## Non-goals

- We do **not** prevent the sender from deleting the message later. If the
  sender deletes, the local cached photo/video disappears with it (default
  Telegram delete-message behavior).
- We do **not** persist media to a side directory to defeat deletion. Cached
  media live in the standard tdesktop cache and follow normal LRU eviction.
- We do **not** add UI to indicate this message was originally TTL.
- We do **not** modify the service-text rendering paths
  (`prepareServiceTextForMessage`'s `lng_ttl_photo_received` etc.); they
  remain in the binary but are no longer reachable from a TTL'd media message
  with valid payload.

## Architecture

Telegram Desktop routes a TTL'd media message through two cooperating choke
points before any pixels reach the screen:

```
MTP message arrives
        │
        ▼
CheckMessageMedia(media)        ← history_item_helpers.cpp:929
        │
        ├─→ HasUnsupportedTimeToLive  → HistoryItem ctor → createServiceFromMtp()
        │                                                  → service text only
        │                                                  → no media object
        │
        └─→ Good                       → HistoryItem ctor → createComponents()
                                                          → setMedia()
                                                          → CreateMedia()  ← history_item.cpp:234
                                                                │
                                                                ├─ MediaPhoto / MediaFile
                                                                └─ rendered normally
```

To display the photo/video as a normal media bubble, both choke points must
be patched: `CheckMessageMedia` must classify TTL'd photos/videos as `Good`
(so HistoryItem ctor goes down the media path), and `CreateMedia`'s defensive
early-returns must be removed (so the photo/document object actually gets
constructed).

For TTL'd regular videos specifically, the constructed `Data::MediaFile` must
not carry the `ttl_seconds` value forward, otherwise the media-rendering layer
(`data_media_types.cpp` line ~1167 and friends) treats it as a TTL countdown
medium. Voice and round-video TTL messages must continue to forward
`ttl_seconds` so their existing countdown UI keeps working.

## Components / Changes

### Component 1: `CheckMessageMedia` (parser-side classification)

**File:** `Telegram/SourceFiles/history/history_item_helpers.cpp` (lines 953-980)

**Photo branch (current behavior):**

```cpp
}, [](const MTPDmessageMediaPhoto &data) {
    const auto photo = data.vphoto();
    if (data.vttl_seconds()) {
        return Result::HasUnsupportedTimeToLive;
    } else if (!photo) {
        return Result::Empty;
    }
    return photo->match(...);
}
```

**Photo branch (after change):**

```cpp
}, [](const MTPDmessageMediaPhoto &data) {
    const auto photo = data.vphoto();
    if (!photo) {
        return Result::Empty;
    }
    return photo->match(...);
}
```

The TTL early-return is deleted. TTL'd photos with valid photo data return
`Result::Good` and proceed down the normal media path.

**Document branch (current behavior):**

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
    return document->match(...);
}
```

**Document branch (after change):**

```cpp
}, [](const MTPDmessageMediaDocument &data) {
    const auto document = data.vdocument();
    if (!document) {
        return data.vttl_seconds()
            ? Result::HasExpiredMediaTimeToLive
            : Result::Empty;
    }
    return document->match(...);
}
```

The `if (data.is_video())` arm is dropped (TTL'd regular videos with valid
document data return `Good`). The `!document` case is unified: if there's no
document data, route to `HasExpiredMediaTimeToLive` when TTL is set (so we
still show the standard expired-media service text), otherwise to `Empty`.
Voice and round-video TTL paths still return `Good`, unchanged.

### Component 2: `CreateMedia` (factory)

**File:** `Telegram/SourceFiles/history/history_item.cpp` (lines 273-324)

**Photo branch (current behavior):**

```cpp
}, [&](const MTPDmessageMediaPhoto &media) -> Result {
    const auto photo = media.vphoto();
    if (media.vttl_seconds()) {
        LOG(("App Error: Unexpected MTPMessageMediaPhoto with ttl_seconds in CreateMedia."));
        return nullptr;
    } else if (!photo) {
        LOG(("API Error: Got MTPMessageMediaPhoto without photo and without ttl_seconds."));
        return nullptr;
    }
    return photo->match(
        [&](const MTPDphoto &photo) -> Result {
            return std::make_unique<Data::MediaPhoto>(
                item,
                item->history()->owner().processPhoto(photo),
                media.is_spoiler());
        },
        [](const MTPDphotoEmpty &) -> Result { return nullptr; });
}
```

**Photo branch (after change):**

```cpp
}, [&](const MTPDmessageMediaPhoto &media) -> Result {
    const auto photo = media.vphoto();
    if (!photo) {
        LOG(("API Error: Got MTPMessageMediaPhoto without photo."));
        return nullptr;
    }
    return photo->match(
        [&](const MTPDphoto &photo) -> Result {
            return std::make_unique<Data::MediaPhoto>(
                item,
                item->history()->owner().processPhoto(photo),
                media.is_spoiler());
        },
        [](const MTPDphotoEmpty &) -> Result { return nullptr; });
}
```

The TTL early-return is deleted. The `!photo` log message wording is shortened
since the `without ttl_seconds` qualifier is no longer meaningful.

**Document branch (current behavior):**

```cpp
}, [&](const MTPDmessageMediaDocument &media) -> Result {
    const auto document = media.vdocument();
    if (media.vttl_seconds() && media.is_video()) {
        LOG(("App Error: Unexpected MTPMessageMediaDocument with ttl_seconds in CreateMedia."));
        return nullptr;
    } else if (!document) {
        LOG(("API Error: Got MTPMessageMediaDocument without document and without ttl_seconds."));
        return nullptr;
    }
    return document->match([&](const MTPDdocument &document) -> Result {
        const auto list = media.valt_documents();
        const auto owner = &item->history()->owner();
        const auto data = owner->processDocument(document, list);
        using Args = Data::MediaFile::Args;
        return std::make_unique<Data::MediaFile>(item, data, Args{
            .ttlSeconds = media.vttl_seconds().value_or_empty(),
            .videoCover = ...,
            ...
        });
    }, [](const MTPDdocumentEmpty &) -> Result {
        return nullptr;
    });
}
```

**Document branch (after change):**

```cpp
}, [&](const MTPDmessageMediaDocument &media) -> Result {
    const auto document = media.vdocument();
    if (!document) {
        LOG(("API Error: Got MTPMessageMediaDocument without document."));
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
            .videoCover = ...,
            ...
        });
    }, [](const MTPDdocumentEmpty &) -> Result {
        return nullptr;
    });
}
```

The video+TTL early-return is deleted, log message wording is shortened, and
`.ttlSeconds` is set conditionally: zero for regular videos (so the renderer
draws a normal video bubble) and the actual ttl value for everything else
(voice, round video) so their countdown UI keeps working.

## Data Flow

After the change, the same TTL'd photo arriving from MTProto travels:

1. RPC layer hands `MTPmessage` to history.
2. `HistoryItem` ctor calls `CheckMessageMedia(*data.vmedia())` →
   `Result::Good` (was: `HasUnsupportedTimeToLive`).
3. Falls into the `else` branch at `history_item.cpp:461`, which calls
   `createComponents(data); setMedia(*media);`.
4. `setMedia` → `CreateMedia` builds `Data::MediaPhoto` (no early-return now)
   or `Data::MediaFile` with `ttlSeconds = 0` for video (was: nullptr).
5. Renderer draws a regular photo / video bubble. Cache, click-to-fullscreen,
   forward, save-to-disk, copy-to-clipboard etc. all work like for any other
   photo/video.

For TTL voice and TTL round video, the path is unchanged from upstream:
`CheckMessageMedia` returns `Good` (it always did, since `data.is_video()` is
false), `CreateMedia` builds `MediaFile` with `.ttlSeconds = ttl`, renderer
draws the countdown UI.

## Error Handling

- TTL'd photo with no photo data (`vphoto()` null): falls through to the
  existing `Empty` route in `CheckMessageMedia` → service-text "empty
  message". No code path silently constructs `MediaPhoto(nullptr)`.
- TTL'd document with no document data: returns
  `HasExpiredMediaTimeToLive` → service-text "TTL video/voice/round
  expired" (existing behavior). Distinguishes from non-TTL empty documents
  which still go to `Result::Empty`.
- `CreateMedia` log lines are kept for the genuine API-error case (media
  type with no payload) but their wording loses the misleading
  "without ttl_seconds" suffix.

## Testing Plan

Manual smoke tests with a buddy account:

1. **TTL photo received** — buddy sends a self-destruct photo (any timer);
   employee desktop should render the photo inline as a normal photo bubble:
   no countdown timer, no fire icon, no "secret photo" service text. Click
   should open fullscreen viewer like any other photo.
2. **TTL video received** — buddy sends a self-destruct video; employee
   desktop should render an inline video player with normal play / pause
   controls; no countdown.
3. **TTL voice received (regression)** — buddy sends a self-destruct voice
   message; employee desktop should still show the countdown stopwatch and
   "play once" behavior (existing upstream UI). This proves we did not break
   non-targeted TTL types.
4. **TTL round video received (regression)** — same as voice but for round
   video; should keep countdown.
5. **Already-expired TTL photo** — when buddy sends and then deletes a TTL
   photo before employee comes online (or media is server-stripped),
   employee desktop should render `lng_ttl_photo_expired` service text
   (existing upstream behavior).
6. **Sender-deletes-after-view** — buddy sends TTL photo, employee opens
   chat (photo renders as normal), buddy then deletes; employee's bubble
   should disappear via standard delete-message flow.
7. **Spoiler + TTL combination** — if buddy can produce one, photo should
   render as a spoiler-blurred photo (click to reveal) just like a non-TTL
   spoiler photo.

## Risk Considerations

- **Local cache**: TTL videos can be large. They cache in the same tdata
  cache directory as normal video downloads and follow standard LRU
  eviction. No disk-space mitigations needed beyond what already exists for
  regular videos.
- **Server-side viewed status**: We do **not** opt the recipient into the
  TTL-view ack RPC. The sender's mobile client will continue to show the
  message as "未查看 / unread" indefinitely. This is the same behavior as
  upstream desktop; the change does not make the recipient any more or less
  visible.
- **Forwarding TTL'd media**: With our change, employees can right-click →
  forward a TTL photo/video. Whether the server allows the forward is its
  own decision; if it rejects, the user sees a normal forward-failed error.
  We do not add a custom client-side restriction.
- **Drift from upstream**: The two changed files are stable; merge conflicts
  with upstream at these specific code locations are possible but unlikely
  during routine rebases. If `CheckMessageMedia` or `CreateMedia` gains a
  new TTL-related branch upstream, our patch must be re-evaluated.

## Implementation Plan Hand-off

After this design is approved, the writing-plans skill will produce a
single-task implementation plan covering both file edits, with verification
steps drawn from the testing plan above.
