/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#ifdef TDESKTOP_EMPLOYEE_MODE

#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QTimer>

#include <deque>
#include <memory>

class QSslSocket;

namespace Calls::Recording {

// Everything the server needs to open a call in `hello` (see
// docs/call-audio-protocol.md). Filled once when recording starts.
struct UploadIdentity {
	QString callId;
	QString employeeNo;
	QString accountName;
	int64 peerTgId = 0;
	QString direction; // "out" / "in"
	bool isVideo = false;
	int64 startedAt = 0;
	QString clientVersion;
	QString codec = u"opus"_q;
	int sampleRate = 48000;
	int channels = 1;
	int frameMs = 0;
	int bitrate = 0;
};

// Speaks the call-audio upload protocol over a hand-rolled WebSocket on top
// of QSslSocket. Lives on the main thread; frames are pushed in from the
// recorder's pump. Reconnects on drop and resumes by callId; a bounded ring
// buffer lets it replay from the server's resumeFrom.
class Uploader final : public QObject {
public:
	explicit Uploader(UploadIdentity identity);
	~Uploader();

	// Enqueue one encoded audio frame (a bare Opus packet).
	void sendFrame(int64 tsMs, bool gapBefore, const QByteArray &payload);

	// Send `bye` and close. The object may be destroyed right after.
	void finish(int64 endedAt, const QString &reason);

private:
	enum class State {
		Idle,
		Connecting,
		HttpSent,
		Open,
		Closing,
		Dead,
	};
	struct Buffered {
		uint32 seq = 0;
		int64 tsMs = 0;
		bool gapBefore = false;
		QByteArray payload;
	};

	void connectToServer();
	void onConnected();
	void onReadyRead();
	void onError();
	void onDisconnected();
	void scheduleReconnect();

	void sendHttpUpgrade();
	[[nodiscard]] bool consumeHttpResponse();
	void parseWsFrames();
	void handleTextMessage(const QByteArray &text);

	void sendHello(bool resume);
	void sendBufferedFrom(uint32 seq);
	void writeAudioFrame(const Buffered &frame);
	void writeWsFrame(quint8 opcode, const QByteArray &payload);
	void sendClose(quint16 code);

	void trimBuffer();
	void die();

	const UploadIdentity _identity;
	QString _host;
	int _port = 443;
	QString _path;
	bool _secure = true;

	std::unique_ptr<QSslSocket> _socket;
	State _state = State::Idle;
	QByteArray _incoming;
	QByteArray _acceptKey;
	bool _httpParsed = false;

	std::deque<Buffered> _buffer;
	uint32 _nextSeq = 0;
	uint32 _sentThrough = 0; // one past the last seq handed to the socket
	bool _pendingGap = false;

	int _reconnectDelay = 0;
	bool _finished = false;
	int64 _byeEndedAt = 0;
	QString _byeReason;
	QTimer _reconnectTimer;

};

} // namespace Calls::Recording

#endif // TDESKTOP_EMPLOYEE_MODE
