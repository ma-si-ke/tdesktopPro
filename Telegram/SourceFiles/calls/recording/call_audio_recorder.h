/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "calls/recording/call_audio_uploader.h"
#include "calls/recording/call_audio_opus.h"
#include "base/timer.h"

#include <memory>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace CallTap {
class PcmSink;
} // namespace CallTap

namespace Calls::Recording {

class RecorderShared;

// Owns the whole client-side recording pipeline for one call: the audio tap
// (via an AudioDeviceDataObserver wrapped around the real ADM), a mono 48 kHz
// mixer, the Opus encoder and the upload connection. Created and driven on the
// main thread; the tap runs on WebRTC audio threads and only touches the
// thread-safe RecorderShared.
class CallAudioRecorder final {
public:
	explicit CallAudioRecorder(UploadIdentity identity);
	~CallAudioRecorder();

	// The PCM sink to hand to CallTap::MakeRecordingCreator. Stays valid for
	// the recorder's lifetime (and, since it is shared, a little beyond).
	[[nodiscard]] std::shared_ptr<CallTap::PcmSink> sink() const;

	void stop(const QString &reason);

	// Fills employeeNo / accountName / callId / clientVersion / startedAt.
	// Returns false when the current account is not an authorized employee,
	// in which case no recording should happen.
	[[nodiscard]] static bool FillEmployeeIdentity(
		UploadIdentity &to,
		not_null<Main::Session*> session);

private:
	void pump();

	UploadIdentity _identity;
	std::shared_ptr<RecorderShared> _shared;
	OpusEncoder _encoder;
	std::unique_ptr<Uploader> _uploader;
	base::Timer _pumpTimer;
	std::vector<float> _pullBuffer;
	int64 _packetCount = 0;
	bool _stopped = false;

};

} // namespace Calls::Recording

#endif // TDESKTOP_EMPLOYEE_MODE
