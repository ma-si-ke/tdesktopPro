/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "ffmpeg/ffmpeg_utility.h"

#include <vector>
#include <cstdint>

namespace Calls::Recording {

// Mono 48 kHz Opus encoder producing bare Opus packets (no container).
// Feed interleaved mono float samples; whole packets are handed back one by
// one. Not thread-safe: drive it from a single thread.
class OpusEncoder final {
public:
	OpusEncoder();
	~OpusEncoder();

	[[nodiscard]] bool valid() const {
		return _codec != nullptr;
	}
	[[nodiscard]] int sampleRate() const {
		return _sampleRate;
	}
	[[nodiscard]] int frameSamples() const {
		return _frameSamples;
	}
	[[nodiscard]] int frameMs() const {
		return _frameSamples
			? int(int64_t(_frameSamples) * 1000 / _sampleRate)
			: 0;
	}

	void encode(
		const float *samples,
		int count,
		const Fn<void(const uint8_t *data, int size)> &packet);

private:
	void drainPackets(const Fn<void(const uint8_t*, int)> &packet);

	FFmpeg::CodecPointer _codec;
	FFmpeg::FramePointer _frame;
	FFmpeg::Packet _packet;
	std::vector<float> _buffer;
	int _sampleRate = 48000;
	int _frameSamples = 0;
	int64_t _pts = 0;

};

} // namespace Calls::Recording

#endif // TDESKTOP_EMPLOYEE_MODE
