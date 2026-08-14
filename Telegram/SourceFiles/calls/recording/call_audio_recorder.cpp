/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/recording/call_audio_recorder.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "calls/recording/call_tap.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/version.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "intro/employee/employee_permissions.h"
#include "intro/employee/employee_prefs.h"
#include "base/debug_log.h"

#include <QtCore/QUuid>
#include <QtCore/QDateTime>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>

namespace Calls::Recording {
namespace {

constexpr auto kOutRate = 48000;
constexpr auto kPumpIntervalMs = crl::time(60);
constexpr auto kCaptureCapSamples = 48000 / 5; // 200ms of look-behind.
constexpr auto kMixedCapSamples = 48000 * 5; // Guard if the pump stalls.

} // namespace

// Thread-safe glue between the WebRTC audio threads (onCapture/onRender) and
// the main-thread pump (pullMixed). Render is treated as the clock: each
// rendered sample is mixed with the aligned captured sample.
class RecorderShared final : public CallTap::PcmSink {
public:
	void onCapture(
		const int16_t *samples,
		int frames,
		int channels,
		int sampleRate) override;
	void onRender(
		const int16_t *samples,
		int frames,
		int channels,
		int sampleRate) override;

	void pullMixed(std::vector<float> &out);

private:
	static void ToMono48k(
		const int16_t *data,
		int frames,
		int channels,
		int rate,
		std::vector<float> &out);

	std::mutex _mutex;
	std::deque<float> _capture;
	std::deque<float> _mixed;
	std::vector<float> _scratch;

};

void RecorderShared::ToMono48k(
		const int16_t *data,
		int frames,
		int channels,
		int rate,
		std::vector<float> &out) {
	if (frames <= 0 || channels <= 0 || rate <= 0) {
		return;
	}
	auto mono = std::vector<float>(frames);
	for (auto i = 0; i != frames; ++i) {
		auto sum = 0;
		for (auto c = 0; c != channels; ++c) {
			sum += data[i * channels + c];
		}
		mono[i] = float(sum) / (channels * 32768.0f);
	}
	if (rate == kOutRate) {
		out.insert(out.end(), mono.begin(), mono.end());
		return;
	}
	const auto outN = int(int64_t(frames) * kOutRate / rate);
	for (auto k = 0; k != outN; ++k) {
		const auto srcPos = double(k) * rate / double(kOutRate);
		auto i = int(srcPos);
		if (i >= frames) {
			i = frames - 1;
		}
		const auto frac = float(srcPos - i);
		const auto a = mono[i];
		const auto b = (i + 1 < frames) ? mono[i + 1] : mono[frames - 1];
		out.push_back(a + (b - a) * frac);
	}
}

void RecorderShared::onCapture(
		const int16_t *samples,
		int frames,
		int channels,
		int sampleRate) {
	auto converted = std::vector<float>();
	ToMono48k(samples, frames, channels, sampleRate, converted);
	if (converted.empty()) {
		return;
	}
	auto lock = std::scoped_lock(_mutex);
	_capture.insert(_capture.end(), converted.begin(), converted.end());
	while (int(_capture.size()) > kCaptureCapSamples) {
		_capture.pop_front();
	}
}

void RecorderShared::onRender(
		const int16_t *samples,
		int frames,
		int channels,
		int sampleRate) {
	auto converted = std::vector<float>();
	ToMono48k(samples, frames, channels, sampleRate, converted);
	if (converted.empty()) {
		return;
	}
	auto lock = std::scoped_lock(_mutex);
	for (const auto render : converted) {
		auto value = render;
		if (!_capture.empty()) {
			value += _capture.front();
			_capture.pop_front();
		}
		_mixed.push_back(std::clamp(value, -1.0f, 1.0f));
	}
	while (int(_mixed.size()) > kMixedCapSamples) {
		_mixed.pop_front();
	}
}

void RecorderShared::pullMixed(std::vector<float> &out) {
	out.clear();
	auto lock = std::scoped_lock(_mutex);
	out.assign(_mixed.begin(), _mixed.end());
	_mixed.clear();
}

CallAudioRecorder::CallAudioRecorder(UploadIdentity identity)
: _identity(std::move(identity))
, _shared(std::make_shared<RecorderShared>()) {
	if (!_encoder.valid()) {
		LOG(("Call Rec Error: no encoder, recording disabled."));
		return;
	}
	_identity.codec = u"opus"_q;
	_identity.sampleRate = _encoder.sampleRate();
	_identity.channels = 1;
	_identity.frameMs = _encoder.frameMs();
	_identity.bitrate = 24000;

	_uploader = std::make_unique<Uploader>(_identity);

	_pumpTimer.setCallback([=] { pump(); });
	_pumpTimer.callEach(kPumpIntervalMs);
}

CallAudioRecorder::~CallAudioRecorder() {
	if (!_stopped) {
		stop(u"error"_q);
	}
}

std::shared_ptr<CallTap::PcmSink> CallAudioRecorder::sink() const {
	return _shared;
}

void CallAudioRecorder::pump() {
	if (!_uploader || !_encoder.valid()) {
		return;
	}
	_shared->pullMixed(_pullBuffer);
	if (_pullBuffer.empty()) {
		return;
	}
	const auto frameMs = _encoder.frameMs();
	_encoder.encode(
		_pullBuffer.data(),
		int(_pullBuffer.size()),
		[&](const uint8_t *data, int size) {
			const auto tsMs = int64(_packetCount) * frameMs;
			++_packetCount;
			_uploader->sendFrame(
				tsMs,
				false,
				QByteArray(reinterpret_cast<const char*>(data), size));
		});
}

void CallAudioRecorder::stop(const QString &reason) {
	if (_stopped) {
		return;
	}
	_stopped = true;
	_pumpTimer.cancel();
	pump(); // Flush whatever is already mixed.
	if (_uploader) {
		_uploader->finish(QDateTime::currentMSecsSinceEpoch(), reason);
	}
}

bool CallAudioRecorder::FillEmployeeIdentity(
		UploadIdentity &to,
		not_null<Main::Session*> session) {
	if (!session->account().employeePermissions().authorized()) {
		return false;
	}
	to.employeeNo = Intro::Employee::Prefs::LastUsername();
	to.accountName = Core::App().settings().customDeviceModel();
	if (to.accountName.isEmpty()) {
		to.accountName = to.employeeNo;
	}
	to.callId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	to.clientVersion = QString::fromLatin1(AppVersionStr);
	to.startedAt = QDateTime::currentMSecsSinceEpoch();
	return true;
}

} // namespace Calls::Recording

#endif // TDESKTOP_EMPLOYEE_MODE
