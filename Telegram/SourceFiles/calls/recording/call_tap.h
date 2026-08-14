/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "webrtc/webrtc_create_adm.h"

#include <functional>
#include <memory>
#include <cstdint>

namespace webrtc {
class TaskQueueFactory;
} // namespace webrtc

namespace CallTap {

// Plain, webrtc-free sink for the tapped call audio. The two streams arrive as
// 16-bit interleaved PCM on WebRTC audio threads: capture is the local
// microphone (the employee), render is the mixed remote audio (the peer).
// Implementations must be thread-safe and cheap.
class PcmSink {
public:
	virtual ~PcmSink() = default;

	virtual void onCapture(
		const int16_t *samples,
		int frames,
		int channels,
		int sampleRate) = 0;
	virtual void onRender(
		const int16_t *samples,
		int frames,
		int channels,
		int sampleRate) = 0;
};

// Same shape as Webrtc::AudioDeviceModuleCreator, drop-in for the tgcalls
// descriptor's `createAudioDeviceModule`. The produced ADM is wrapped so its
// capture/render PCM is delivered to `sink`. A null sink yields the plain,
// unwrapped creator, so callers can wire this in unconditionally.
[[nodiscard]] auto MakeRecordingCreator(
	Fn<void(Fn<void(Webrtc::DeviceResolvedId)>)> saveSetDeviceIdCallback,
	std::shared_ptr<PcmSink> sink)
-> std::function<Webrtc::AudioDeviceModulePtr(webrtc::TaskQueueFactory*)>;

} // namespace CallTap
