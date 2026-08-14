/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/recording/call_audio_opus.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "base/debug_log.h"

#include <cstring>

namespace Calls::Recording {
namespace {

constexpr auto kSampleRate = 48000;
constexpr auto kBitRate = 24000;
constexpr auto kFallbackFrameSamples = 960; // 20ms at 48 kHz.

} // namespace

OpusEncoder::OpusEncoder()
: _frame(FFmpeg::MakeFramePointer()) {
	const auto codec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
	if (!codec) {
		LOG(("Call Rec Error: no Opus encoder."));
		return;
	}
	auto context = FFmpeg::CodecPointer(avcodec_alloc_context3(codec));
	if (!context) {
		LOG(("Call Rec Error: avcodec_alloc_context3 failed."));
		return;
	}
	context->sample_fmt = AV_SAMPLE_FMT_FLTP;
	context->bit_rate = kBitRate;
	context->sample_rate = kSampleRate;
	context->ch_layout = AV_CHANNEL_LAYOUT_MONO;
	context->time_base = AVRational{ 1, kSampleRate };

	const auto error = FFmpeg::AvErrorWrap(
		avcodec_open2(context.get(), codec, nullptr));
	if (error) {
		LOG(("Call Rec Error: avcodec_open2: %1").arg(error.text()));
		return;
	}
	_sampleRate = context->sample_rate;
	_frameSamples = (context->frame_size > 0)
		? context->frame_size
		: kFallbackFrameSamples;

	_frame->nb_samples = _frameSamples;
	_frame->format = AV_SAMPLE_FMT_FLTP;
	_frame->ch_layout = AV_CHANNEL_LAYOUT_MONO;
	_frame->sample_rate = _sampleRate;
	const auto bufferError = FFmpeg::AvErrorWrap(
		av_frame_get_buffer(_frame.get(), 0));
	if (bufferError) {
		LOG(("Call Rec Error: av_frame_get_buffer: %1"
			).arg(bufferError.text()));
		return;
	}
	_codec = std::move(context);
	_buffer.reserve(_frameSamples * 2);
}

OpusEncoder::~OpusEncoder() = default;

void OpusEncoder::encode(
		const float *samples,
		int count,
		const Fn<void(const uint8_t*, int)> &packet) {
	if (!valid() || count <= 0) {
		return;
	}
	_buffer.insert(_buffer.end(), samples, samples + count);

	auto offset = 0;
	while (int(_buffer.size()) - offset >= _frameSamples) {
		if (FFmpeg::AvErrorWrap(av_frame_make_writable(_frame.get()))) {
			break;
		}
		auto plane = reinterpret_cast<float*>(_frame->data[0]);
		memcpy(
			plane,
			_buffer.data() + offset,
			_frameSamples * sizeof(float));
		_frame->pts = _pts;
		_pts += _frameSamples;
		offset += _frameSamples;

		const auto sendError = FFmpeg::AvErrorWrap(
			avcodec_send_frame(_codec.get(), _frame.get()));
		if (sendError && sendError.code() != AVERROR(EAGAIN)) {
			LOG(("Call Rec Error: avcodec_send_frame: %1"
				).arg(sendError.text()));
			break;
		}
		drainPackets(packet);
	}
	if (offset > 0) {
		_buffer.erase(_buffer.begin(), _buffer.begin() + offset);
	}
}

void OpusEncoder::drainPackets(const Fn<void(const uint8_t*, int)> &packet) {
	auto &fields = _packet.fields();
	while (true) {
		const auto error = FFmpeg::AvErrorWrap(
			avcodec_receive_packet(_codec.get(), &fields));
		if (error.code() == AVERROR(EAGAIN)
			|| error.code() == AVERROR_EOF) {
			return;
		} else if (error) {
			LOG(("Call Rec Error: avcodec_receive_packet: %1"
				).arg(error.text()));
			return;
		}
		if (fields.data && fields.size > 0) {
			packet(fields.data, fields.size);
		}
		av_packet_unref(&fields);
	}
}

} // namespace Calls::Recording

#endif // TDESKTOP_EMPLOYEE_MODE
