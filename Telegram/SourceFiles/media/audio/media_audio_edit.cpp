/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "media/audio/media_audio_edit.h"

#include "ffmpeg/ffmpeg_bytes_io_wrap.h"
#include "ffmpeg/ffmpeg_utility.h"

namespace Media {

[[nodiscard]] AudioEditResult TrimAudioToRange(
		const QByteArray &content,
		crl::time from,
		crl::time till) {
	using namespace FFmpeg;

	if (content.isEmpty() || (from < 0) || (till <= from)) {
		return {};
	}

	auto inputWrap = ReadBytesWrap{
		.size = content.size(),
		.data = reinterpret_cast<const uchar*>(content.constData()),
	};
	auto input = MakeFormatPointer(
		&inputWrap,
		&ReadBytesWrap::Read,
		nullptr,
		&ReadBytesWrap::Seek);
	if (!input) {
		return {};
	}

	auto error = AvErrorWrap(avformat_find_stream_info(input.get(), nullptr));
	if (error) {
		LogError(u"avformat_find_stream_info"_q, error);
		return {};
	}

	const auto streamId = av_find_best_stream(
		input.get(),
		AVMEDIA_TYPE_AUDIO,
		-1,
		-1,
		nullptr,
		0);
	if (streamId < 0) {
		LogError(u"av_find_best_stream"_q, AvErrorWrap(streamId));
		return {};
	}

	const auto inStream = input->streams[streamId];
	auto outputWrap = WriteBytesWrap();
	auto output = MakeWriteFormatPointer(
		static_cast<void*>(&outputWrap),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"opus"_q);
	if (!output) {
		return {};
	}

	const auto outStream = avformat_new_stream(output.get(), nullptr);
	if (!outStream) {
		LogError(u"avformat_new_stream"_q);
		return {};
	}

	error = AvErrorWrap(avcodec_parameters_copy(
		outStream->codecpar,
		inStream->codecpar));
	if (error) {
		LogError(u"avcodec_parameters_copy"_q, error);
		return {};
	}
	outStream->codecpar->codec_tag = 0;
	outStream->time_base = inStream->time_base;

	error = AvErrorWrap(avformat_write_header(output.get(), nullptr));
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		return {};
	}

	const auto fromPts = TimeToPts(from, inStream->time_base);
	const auto tillPts = TimeToPts(till, inStream->time_base);
	auto firstPts = int64(AV_NOPTS_VALUE);
	auto firstDts = int64(AV_NOPTS_VALUE);
	auto lastPts = std::numeric_limits<int64>::min();
	auto lastDts = std::numeric_limits<int64>::min();
	auto durationPts = int64(0);
	auto copied = 0;

	auto packet = AVPacket();
	av_init_packet(&packet);
	while (true) {
		error = AvErrorWrap(av_read_frame(input.get(), &packet));
		if (error.code() == AVERROR_EOF) {
			break;
		} else if (error) {
			LogError(u"av_read_frame"_q, error);
			return {};
		}
		const auto guard = gsl::finally([&] {
			av_packet_unref(&packet);
		});
		if (packet.stream_index != streamId) {
			continue;
		}

		const auto packetStart = (packet.pts != AV_NOPTS_VALUE)
			? packet.pts
			: packet.dts;
		const auto packetDuration = std::max(int64(packet.duration), int64());
		const auto packetEnd = (packetStart != AV_NOPTS_VALUE)
			? (packetStart + packetDuration)
			: AV_NOPTS_VALUE;
		if ((packetStart != AV_NOPTS_VALUE) && (packetStart >= tillPts)) {
			break;
		}
		if ((packetEnd != AV_NOPTS_VALUE) && (packetEnd <= fromPts)) {
			continue;
		}

		if (packet.pts != AV_NOPTS_VALUE) {
			if (firstPts == AV_NOPTS_VALUE) {
				firstPts = packet.pts;
			}
			packet.pts -= firstPts;
			if (packet.pts < 0) {
				packet.pts = 0;
			}
			if (packet.pts <= lastPts) {
				packet.pts = lastPts + 1;
			}
			lastPts = packet.pts;
		}
		if (packet.dts != AV_NOPTS_VALUE) {
			if (firstDts == AV_NOPTS_VALUE) {
				firstDts = packet.dts;
			}
			packet.dts -= firstDts;
			if (packet.dts < 0) {
				packet.dts = 0;
			}
			if (packet.dts <= lastDts) {
				packet.dts = lastDts + 1;
			}
			lastDts = packet.dts;
		}

		const auto packetPosition = (packet.pts != AV_NOPTS_VALUE)
			? packet.pts
			: packet.dts;
		if (packetPosition != AV_NOPTS_VALUE) {
			durationPts = std::max(
				durationPts,
				packetPosition + std::max(int64(packet.duration), int64()));
		}

		packet.stream_index = outStream->index;
		error = AvErrorWrap(av_interleaved_write_frame(output.get(), &packet));
		if (error) {
			LogError(u"av_interleaved_write_frame"_q, error);
			return {};
		}
		++copied;
	}

	if (!copied) {
		return {};
	}

	error = AvErrorWrap(av_write_trailer(output.get()));
	if (error) {
		LogError(u"av_write_trailer"_q, error);
		return {};
	}

	auto result = AudioEditResult();
	result.content = std::move(outputWrap.content);
	result.waveform = audioCountWaveform(Core::FileLocation(), result.content);
	result.duration = durationPts
		? PtsToTimeCeil(durationPts, outStream->time_base)
		: (till - from);
	return result;
}

[[nodiscard]] AudioEditResult ConcatAudio(
		const QByteArray &first,
		const QByteArray &second) {
	using namespace FFmpeg;

	if (first.isEmpty() || second.isEmpty()) {
		return {};
	}

	auto firstWrap = ReadBytesWrap{
		.size = first.size(),
		.data = reinterpret_cast<const uchar*>(first.constData()),
	};
	auto firstInput = MakeFormatPointer(
		&firstWrap,
		&ReadBytesWrap::Read,
		nullptr,
		&ReadBytesWrap::Seek);
	if (!firstInput) {
		return {};
	}

	auto secondWrap = ReadBytesWrap{
		.size = second.size(),
		.data = reinterpret_cast<const uchar*>(second.constData()),
	};
	auto secondInput = MakeFormatPointer(
		&secondWrap,
		&ReadBytesWrap::Read,
		nullptr,
		&ReadBytesWrap::Seek);
	if (!secondInput) {
		return {};
	}

	const auto prepareStream = [](not_null<AVFormatContext*> input) {
		auto error = AvErrorWrap(avformat_find_stream_info(input, nullptr));
		if (error) {
			LogError(u"avformat_find_stream_info"_q, error);
			return static_cast<AVStream*>(nullptr);
		}
		const auto streamId = av_find_best_stream(
			input,
			AVMEDIA_TYPE_AUDIO,
			-1,
			-1,
			nullptr,
			0);
		if (streamId < 0) {
			LogError(u"av_find_best_stream"_q, AvErrorWrap(streamId));
			return static_cast<AVStream*>(nullptr);
		}
		return input->streams[streamId];
	};
	const auto firstStream = prepareStream(firstInput.get());
	if (!firstStream) {
		return {};
	}
	const auto secondStream = prepareStream(secondInput.get());
	if (!secondStream) {
		return {};
	}

	auto outputWrap = WriteBytesWrap();
	auto output = MakeWriteFormatPointer(
		static_cast<void*>(&outputWrap),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"opus"_q);
	if (!output) {
		return {};
	}
	const auto outStream = avformat_new_stream(output.get(), nullptr);
	if (!outStream) {
		LogError(u"avformat_new_stream"_q);
		return {};
	}

	auto error = AvErrorWrap(avcodec_parameters_copy(
		outStream->codecpar,
		firstStream->codecpar));
	if (error) {
		LogError(u"avcodec_parameters_copy"_q, error);
		return {};
	}
	outStream->codecpar->codec_tag = 0;
	outStream->time_base = firstStream->time_base;

	error = AvErrorWrap(avformat_write_header(output.get(), nullptr));
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		return {};
	}

	auto offsetPts = int64(0);
	auto durationPts = int64(0);
	auto lastPts = std::numeric_limits<int64>::min();
	auto lastDts = std::numeric_limits<int64>::min();
	auto copied = 0;

	const auto append = [&](
			not_null<AVFormatContext*> input,
			not_null<AVStream*> inStream) {
		auto firstPts = int64(AV_NOPTS_VALUE);
		auto firstDts = int64(AV_NOPTS_VALUE);
		auto sourceEndPts = offsetPts;

		auto packet = AVPacket();
		av_init_packet(&packet);
		while (true) {
			auto error = AvErrorWrap(av_read_frame(input, &packet));
			if (error.code() == AVERROR_EOF) {
				break;
			} else if (error) {
				LogError(u"av_read_frame"_q, error);
				return false;
			}
			const auto guard = gsl::finally([&] {
				av_packet_unref(&packet);
			});
			if (packet.stream_index != inStream->index) {
				continue;
			}

			if (packet.pts != AV_NOPTS_VALUE) {
				if (firstPts == AV_NOPTS_VALUE) {
					firstPts = packet.pts;
				}
				packet.pts -= firstPts;
				if (packet.pts < 0) {
					packet.pts = 0;
				}
				packet.pts = av_rescale_q(
					packet.pts,
					inStream->time_base,
					outStream->time_base);
				packet.pts += offsetPts;
				if (packet.pts <= lastPts) {
					packet.pts = lastPts + 1;
				}
				lastPts = packet.pts;
			}
			if (packet.dts != AV_NOPTS_VALUE) {
				if (firstDts == AV_NOPTS_VALUE) {
					firstDts = packet.dts;
				}
				packet.dts -= firstDts;
				if (packet.dts < 0) {
					packet.dts = 0;
				}
				packet.dts = av_rescale_q(
					packet.dts,
					inStream->time_base,
					outStream->time_base);
				packet.dts += offsetPts;
				if (packet.dts <= lastDts) {
					packet.dts = lastDts + 1;
				}
				lastDts = packet.dts;
			}
			packet.duration = (packet.duration > 0)
				? av_rescale_q(
					packet.duration,
					inStream->time_base,
					outStream->time_base)
				: 0;

			const auto packetPosition = (packet.pts != AV_NOPTS_VALUE)
				? packet.pts
				: packet.dts;
			if (packetPosition != AV_NOPTS_VALUE) {
				const auto packetEnd = packetPosition
					+ std::max(int64(packet.duration), int64());
				durationPts = std::max(durationPts, packetEnd);
				sourceEndPts = std::max(sourceEndPts, packetEnd);
			}

			packet.stream_index = outStream->index;
			error = AvErrorWrap(av_interleaved_write_frame(output.get(), &packet));
			if (error) {
				LogError(u"av_interleaved_write_frame"_q, error);
				return false;
			}
			++copied;
		}

		offsetPts = sourceEndPts;
		return true;
	};
	if (!append(firstInput.get(), firstStream)
		|| !append(secondInput.get(), secondStream)) {
		return {};
	}
	if (!copied) {
		return {};
	}

	error = AvErrorWrap(av_write_trailer(output.get()));
	if (error) {
		LogError(u"av_write_trailer"_q, error);
		return {};
	}

	auto result = AudioEditResult();
	result.content = std::move(outputWrap.content);
	result.waveform = audioCountWaveform(Core::FileLocation(), result.content);
	result.duration = durationPts
		? PtsToTimeCeil(durationPts, outStream->time_base)
		: 0;
	return result;
}

[[nodiscard]] AudioEditResult ConvertToVoiceMessage(
		const QByteArray &content) {
	using namespace FFmpeg;

	constexpr auto kSampleRate = 48000;
	constexpr auto kBitRate = 32000;
	constexpr auto kFallbackFrame = 960;

	if (content.isEmpty()) {
		return {};
	}

	auto inputWrap = ReadBytesWrap{
		.size = content.size(),
		.data = reinterpret_cast<const uchar*>(content.constData()),
	};
	auto input = MakeFormatPointer(
		&inputWrap,
		&ReadBytesWrap::Read,
		nullptr,
		&ReadBytesWrap::Seek);
	if (!input) {
		return {};
	}

	auto error = AvErrorWrap(avformat_find_stream_info(input.get(), nullptr));
	if (error) {
		LogError(u"avformat_find_stream_info"_q, error);
		return {};
	}

	const auto streamId = av_find_best_stream(
		input.get(),
		AVMEDIA_TYPE_AUDIO,
		-1,
		-1,
		nullptr,
		0);
	if (streamId < 0) {
		LogError(u"av_find_best_stream"_q, AvErrorWrap(streamId));
		return {};
	}
	const auto inStream = input->streams[streamId];

	auto decoder = MakeCodecPointer({ .stream = inStream });
	if (!decoder) {
		return {};
	}

	const auto encoderCodec = avcodec_find_encoder(AV_CODEC_ID_OPUS);
	if (!encoderCodec) {
		LogError(u"avcodec_find_encoder"_q);
		return {};
	}
	auto encoder = CodecPointer(avcodec_alloc_context3(encoderCodec));
	if (!encoder) {
		LogError(u"avcodec_alloc_context3"_q);
		return {};
	}
	encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;
	encoder->bit_rate = kBitRate;
	encoder->sample_rate = kSampleRate;
	encoder->ch_layout = AV_CHANNEL_LAYOUT_MONO;
	encoder->time_base = AVRational{ 1, kSampleRate };

	auto outputWrap = WriteBytesWrap();
	auto output = MakeWriteFormatPointer(
		static_cast<void*>(&outputWrap),
		nullptr,
		&WriteBytesWrap::Write,
		&WriteBytesWrap::Seek,
		"opus"_q);
	if (!output) {
		return {};
	}
	if (output->oformat->flags & AVFMT_GLOBALHEADER) {
		encoder->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	error = AvErrorWrap(avcodec_open2(encoder.get(), encoderCodec, nullptr));
	if (error) {
		LogError(u"avcodec_open2"_q, error);
		return {};
	}

	const auto outStream = avformat_new_stream(output.get(), nullptr);
	if (!outStream) {
		LogError(u"avformat_new_stream"_q);
		return {};
	}
	error = AvErrorWrap(avcodec_parameters_from_context(
		outStream->codecpar,
		encoder.get()));
	if (error) {
		LogError(u"avcodec_parameters_from_context"_q, error);
		return {};
	}
	outStream->time_base = encoder->time_base;

	auto resample = MakeSwresamplePointer(
		&decoder->ch_layout,
		decoder->sample_fmt,
		decoder->sample_rate,
		&encoder->ch_layout,
		AV_SAMPLE_FMT_FLTP,
		kSampleRate);
	if (!resample) {
		return {};
	}

	error = AvErrorWrap(avformat_write_header(output.get(), nullptr));
	if (error) {
		LogError(u"avformat_write_header"_q, error);
		return {};
	}

	auto samples = std::vector<float>();
	const auto srcRate = decoder->sample_rate ? decoder->sample_rate : kSampleRate;

	const auto appendResampled = [&](AVFrame *in) {
		const auto inCount = in ? in->nb_samples : 0;
		const auto delay = swr_get_delay(resample.get(), srcRate);
		const auto maxOut = int(av_rescale_rnd(
			delay + inCount,
			kSampleRate,
			srcRate,
			AV_ROUND_UP));
		if (maxOut <= 0) {
			return true;
		}
		const auto had = samples.size();
		samples.resize(had + maxOut);
		uint8_t *dstData[1] = {
			reinterpret_cast<uint8_t*>(samples.data() + had),
		};
		const auto inData = in
			? const_cast<const uint8_t**>(in->extended_data)
			: nullptr;
		const auto got = swr_convert(
			resample.get(),
			dstData,
			maxOut,
			inData,
			inCount);
		if (got < 0) {
			LogError(u"swr_convert"_q, AvErrorWrap(got));
			return false;
		}
		samples.resize(had + got);
		return true;
	};

	auto frame = MakeFramePointer();
	const auto decodePacket = [&](AVPacket *packet) {
		const auto sendError = AvErrorWrap(
			avcodec_send_packet(decoder.get(), packet));
		if (sendError
			&& sendError.code() != AVERROR(EAGAIN)
			&& !(packet == nullptr && sendError.code() == AVERROR_EOF)) {
			LogError(u"avcodec_send_packet"_q, sendError);
			return false;
		}
		while (true) {
			const auto recvError = AvErrorWrap(
				avcodec_receive_frame(decoder.get(), frame.get()));
			if (recvError.code() == AVERROR(EAGAIN)
				|| recvError.code() == AVERROR_EOF) {
				break;
			} else if (recvError) {
				LogError(u"avcodec_receive_frame"_q, recvError);
				return false;
			}
			const auto guard = gsl::finally([&] {
				av_frame_unref(frame.get());
			});
			if (!appendResampled(frame.get())) {
				return false;
			}
		}
		return true;
	};

	auto packet = AVPacket();
	av_init_packet(&packet);
	while (true) {
		error = AvErrorWrap(av_read_frame(input.get(), &packet));
		if (error.code() == AVERROR_EOF) {
			break;
		} else if (error) {
			LogError(u"av_read_frame"_q, error);
			return {};
		}
		const auto guard = gsl::finally([&] {
			av_packet_unref(&packet);
		});
		if (packet.stream_index != streamId) {
			continue;
		}
		if (!decodePacket(&packet)) {
			return {};
		}
	}
	if (!decodePacket(nullptr) || !appendResampled(nullptr)) {
		return {};
	}
	if (samples.empty()) {
		return {};
	}

	const auto frameSamples = (encoder->frame_size > 0)
		? encoder->frame_size
		: kFallbackFrame;
	auto encoded = MakeFramePointer();
	auto outPacket = AVPacket();
	av_init_packet(&outPacket);

	const auto drainEncoder = [&] {
		while (true) {
			const auto recvError = AvErrorWrap(
				avcodec_receive_packet(encoder.get(), &outPacket));
			if (recvError.code() == AVERROR(EAGAIN)
				|| recvError.code() == AVERROR_EOF) {
				return true;
			} else if (recvError) {
				LogError(u"avcodec_receive_packet"_q, recvError);
				return false;
			}
			const auto guard = gsl::finally([&] {
				av_packet_unref(&outPacket);
			});
			av_packet_rescale_ts(
				&outPacket,
				encoder->time_base,
				outStream->time_base);
			outPacket.stream_index = outStream->index;
			const auto writeError = AvErrorWrap(av_interleaved_write_frame(
				output.get(),
				&outPacket));
			if (writeError) {
				LogError(u"av_interleaved_write_frame"_q, writeError);
				return false;
			}
		}
	};
	const auto sendFrame = [&](AVFrame *value) {
		const auto sendError = AvErrorWrap(
			avcodec_send_frame(encoder.get(), value));
		if (sendError) {
			LogError(u"avcodec_send_frame"_q, sendError);
			return false;
		}
		return drainEncoder();
	};

	auto pts = int64(0);
	const auto total = int64(samples.size());
	for (auto offset = int64(0); offset < total; offset += frameSamples) {
		const auto count = int(std::min(int64(frameSamples), total - offset));
		av_frame_unref(encoded.get());
		encoded->format = AV_SAMPLE_FMT_FLTP;
		error = AvErrorWrap(av_channel_layout_copy(
			&encoded->ch_layout,
			&encoder->ch_layout));
		if (error) {
			LogError(u"av_channel_layout_copy"_q, error);
			return {};
		}
		encoded->sample_rate = kSampleRate;
		encoded->nb_samples = frameSamples;
		error = AvErrorWrap(av_frame_get_buffer(encoded.get(), 0));
		if (error) {
			LogError(u"av_frame_get_buffer"_q, error);
			return {};
		}
		auto destination = reinterpret_cast<float*>(encoded->data[0]);
		memcpy(destination, samples.data() + offset, count * sizeof(float));
		if (count < frameSamples) {
			memset(
				destination + count,
				0,
				(frameSamples - count) * sizeof(float));
		}
		encoded->pts = pts;
		pts += frameSamples;
		if (!sendFrame(encoded.get())) {
			return {};
		}
	}
	if (!sendFrame(nullptr)) {
		return {};
	}

	error = AvErrorWrap(av_write_trailer(output.get()));
	if (error) {
		LogError(u"av_write_trailer"_q, error);
		return {};
	}

	auto result = AudioEditResult();
	result.content = std::move(outputWrap.content);
	result.waveform = audioCountWaveform(Core::FileLocation(), result.content);
	result.duration = (total * crl::time(1000)) / kSampleRate;
	return result;
}

} // namespace Media
