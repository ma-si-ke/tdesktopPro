/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

namespace Core {

// Address of the internal business server. Shared notes, message
// encryption and masked messages all live on it, so they must never be
// able to point at different machines.
[[nodiscard]] QString DefaultServiceUrl();

// Never ends with a slash and always starts with a scheme; anything
// stored that does not is replaced by the default.
[[nodiscard]] QString ServiceUrl();
void SetServiceUrl(const QString &url);

// Call recording upload service. Independent of the business server above:
// its own host, its own fixed API key. The endpoint is a WebSocket
// (wss://.../stream); see docs/call-audio-protocol.md.
[[nodiscard]] QString DefaultCallAudioUrl();
[[nodiscard]] QString CallAudioUrl();
void SetCallAudioUrl(const QString &url);
[[nodiscard]] QString CallAudioApiKey();

} // namespace Core
