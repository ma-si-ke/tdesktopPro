/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/recording/call_tap.h"

#include "modules/audio_device/include/audio_device_data_observer.h"

namespace CallTap {
namespace {

class Observer final : public webrtc::AudioDeviceDataObserver {
public:
	explicit Observer(std::shared_ptr<PcmSink> sink)
	: _sink(std::move(sink)) {
	}

	void OnCaptureData(
			const void *audio_samples,
			const size_t num_samples,
			const size_t bytes_per_sample,
			const size_t num_channels,
			const uint32_t samples_per_sec) override {
		if (_sink
			&& num_channels > 0
			&& bytes_per_sample == num_channels * 2) {
			_sink->onCapture(
				static_cast<const int16_t*>(audio_samples),
				int(num_samples),
				int(num_channels),
				int(samples_per_sec));
		}
	}

	void OnRenderData(
			const void *audio_samples,
			const size_t num_samples,
			const size_t bytes_per_sample,
			const size_t num_channels,
			const uint32_t samples_per_sec) override {
		if (_sink
			&& num_channels > 0
			&& bytes_per_sample == num_channels * 2) {
			_sink->onRender(
				static_cast<const int16_t*>(audio_samples),
				int(num_samples),
				int(num_channels),
				int(samples_per_sec));
		}
	}

private:
	std::shared_ptr<PcmSink> _sink;

};

} // namespace

auto MakeRecordingCreator(
	Fn<void(Fn<void(Webrtc::DeviceResolvedId)>)> saveSetDeviceIdCallback,
	std::shared_ptr<PcmSink> sink)
-> std::function<Webrtc::AudioDeviceModulePtr(webrtc::TaskQueueFactory*)> {
	return [=](webrtc::TaskQueueFactory *factory)
			-> Webrtc::AudioDeviceModulePtr {
		auto real = Webrtc::CreateAudioDeviceModule(
			factory,
			saveSetDeviceIdCallback);
		if (!real || !sink) {
			return real;
		}
		auto observer = std::make_unique<Observer>(sink);
		return webrtc::CreateAudioDeviceWithDataObserver(
			std::move(real),
			std::move(observer));
	};
}

} // namespace CallTap
