/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "calls/recording/call_audio_uploader.h"

#ifdef TDESKTOP_EMPLOYEE_MODE

#include "core/service_config.h"
#include "base/debug_log.h"

#include <QtCore/QUrl>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonDocument>
#include <QtCore/QCryptographicHash>
#include <QtCore/QRandomGenerator>
#include <QtNetwork/QSslSocket>

#include <algorithm>

namespace Calls::Recording {
namespace {

constexpr auto kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr auto kMaxBuffered = 2000; // ~40s at 20ms frames.
constexpr auto kReconnectStartMs = 500;
constexpr auto kReconnectMaxMs = 8000;
constexpr auto kFrameHeaderSize = 16;
constexpr quint8 kFrameVersion = 0x01;
constexpr quint8 kFlagGapBefore = 0x01;

void PutBE16(QByteArray &out, quint16 value) {
	out.append(char((value >> 8) & 0xFF));
	out.append(char(value & 0xFF));
}

void PutBE32(QByteArray &out, quint32 value) {
	out.append(char((value >> 24) & 0xFF));
	out.append(char((value >> 16) & 0xFF));
	out.append(char((value >> 8) & 0xFF));
	out.append(char(value & 0xFF));
}

void PutBE64(QByteArray &out, quint64 value) {
	for (auto shift = 56; shift >= 0; shift -= 8) {
		out.append(char((value >> shift) & 0xFF));
	}
}

} // namespace

Uploader::Uploader(UploadIdentity identity)
: _identity(std::move(identity)) {
	const auto url = QUrl(Core::CallAudioUrl());
	_secure = (url.scheme() != u"ws"_q);
	_host = url.host();
	_port = url.port(_secure ? 443 : 80);
	_path = url.path();
	if (_path.isEmpty()) {
		_path = u"/"_q;
	}

	_reconnectTimer.setSingleShot(true);
	connect(&_reconnectTimer, &QTimer::timeout, this, [=] {
		connectToServer();
	});

	if (_host.isEmpty()) {
		LOG(("Call Rec Error: empty audio host, recording disabled."));
		_state = State::Dead;
		return;
	}
	connectToServer();
}

Uploader::~Uploader() = default;

void Uploader::connectToServer() {
	if (_state == State::Dead) {
		return;
	}
	_incoming.clear();
	_httpParsed = false;
	_socket = std::make_unique<QSslSocket>();

	const auto raw = _socket.get();
	connect(raw, &QSslSocket::readyRead, this, [=] { onReadyRead(); });
	connect(raw, &QSslSocket::disconnected, this, [=] { onDisconnected(); });
	connect(
		raw,
		&QAbstractSocket::errorOccurred,
		this,
		[=](QAbstractSocket::SocketError) { onError(); });

	_state = State::Connecting;
	if (_secure) {
		connect(raw, &QSslSocket::encrypted, this, [=] { onConnected(); });
		raw->connectToHostEncrypted(_host, _port);
	} else {
		connect(raw, &QSslSocket::connected, this, [=] { onConnected(); });
		raw->connectToHost(_host, _port);
	}
}

void Uploader::onConnected() {
	if (_state != State::Connecting) {
		return;
	}
	sendHttpUpgrade();
	_state = State::HttpSent;
}

void Uploader::sendHttpUpgrade() {
	auto keyBytes = QByteArray(16, Qt::Uninitialized);
	auto *generator = QRandomGenerator::global();
	for (auto i = 0; i != 16; ++i) {
		keyBytes[i] = char(generator->bounded(256));
	}
	const auto key = keyBytes.toBase64();
	_acceptKey = QCryptographicHash::hash(
		key + kWsGuid,
		QCryptographicHash::Sha1).toBase64();

	const auto hostHeader = (_port == (_secure ? 443 : 80))
		? _host
		: (_host + u":"_q + QString::number(_port));
	auto request = QByteArray();
	request += "GET " + _path.toUtf8() + " HTTP/1.1\r\n";
	request += "Host: " + hostHeader.toUtf8() + "\r\n";
	request += "Upgrade: websocket\r\n";
	request += "Connection: Upgrade\r\n";
	request += "Sec-WebSocket-Version: 13\r\n";
	request += "Sec-WebSocket-Key: " + key + "\r\n";
	request += "X-API-Key: " + Core::CallAudioApiKey().toUtf8() + "\r\n";
	request += "\r\n";
	_socket->write(request);
}

void Uploader::onReadyRead() {
	if (!_socket) {
		return;
	}
	_incoming += _socket->readAll();
	if (!_httpParsed) {
		if (!consumeHttpResponse()) {
			return;
		}
	}
	parseWsFrames();
}

bool Uploader::consumeHttpResponse() {
	const auto headerEnd = _incoming.indexOf("\r\n\r\n");
	if (headerEnd < 0) {
		return false;
	}
	const auto head = _incoming.left(headerEnd);
	_incoming.remove(0, headerEnd + 4);
	_httpParsed = true;

	if (!head.startsWith("HTTP/1.1 101")
		&& !head.startsWith("HTTP/1.0 101")) {
		LOG(("Call Rec Error: WS upgrade rejected: %1"
			).arg(QString::fromLatin1(head.left(64))));
		die();
		return false;
	}
	_state = State::Open;
	_reconnectDelay = 0;
	const auto resume = (_nextSeq > 0);
	sendHello(resume);
	return true;
}

void Uploader::parseWsFrames() {
	while (true) {
		if (_incoming.size() < 2) {
			return;
		}
		const auto b0 = quint8(_incoming[0]);
		const auto b1 = quint8(_incoming[1]);
		const auto opcode = quint8(b0 & 0x0F);
		const auto masked = ((b1 & 0x80) != 0);
		auto length = quint64(b1 & 0x7F);
		auto offset = 2;
		if (length == 126) {
			if (_incoming.size() < offset + 2) {
				return;
			}
			length = (quint8(_incoming[offset]) << 8)
				| quint8(_incoming[offset + 1]);
			offset += 2;
		} else if (length == 127) {
			if (_incoming.size() < offset + 8) {
				return;
			}
			length = 0;
			for (auto i = 0; i != 8; ++i) {
				length = (length << 8) | quint8(_incoming[offset + i]);
			}
			offset += 8;
		}
		const auto maskLen = masked ? 4 : 0;
		if (quint64(_incoming.size()) < quint64(offset) + maskLen + length) {
			return;
		}
		auto mask = QByteArray();
		if (masked) {
			mask = _incoming.mid(offset, 4);
			offset += 4;
		}
		auto payload = _incoming.mid(offset, int(length));
		if (masked) {
			for (auto i = 0; i != payload.size(); ++i) {
				payload[i] = char(payload[i] ^ mask[i % 4]);
			}
		}
		_incoming.remove(0, offset + int(length));

		if (opcode == 0x1) {
			handleTextMessage(payload);
		} else if (opcode == 0x8) {
			auto code = quint16(1000);
			if (payload.size() >= 2) {
				code = quint16((quint8(payload[0]) << 8) | quint8(payload[1]));
			}
			LOG(("Call Rec Info: WS closed by server, code %1").arg(code));
			if (_finished
				|| (code >= 4001 && code <= 4004)) {
				die();
			} else {
				if (_socket) {
					_socket->close();
				}
				scheduleReconnect();
			}
			return;
		} else if (opcode == 0x9) {
			writeWsFrame(0xA, payload);
		}
	}
}

void Uploader::handleTextMessage(const QByteArray &text) {
	const auto parsed = QJsonDocument::fromJson(text).object();
	const auto type = parsed.value(u"type"_q).toString();
	if (type == u"ready"_q) {
		const auto resumeFrom = uint32(
			parsed.value(u"resumeFrom"_q).toDouble());
		sendBufferedFrom(resumeFrom);
	} else if (type == u"error"_q) {
		LOG(("Call Rec Error: server error '%1': %2"
			).arg(parsed.value(u"code"_q).toString()
			).arg(parsed.value(u"message"_q).toString()));
	}
}

void Uploader::sendHello(bool resume) {
	auto audio = QJsonObject();
	audio.insert(u"codec"_q, _identity.codec);
	audio.insert(u"sampleRate"_q, _identity.sampleRate);
	audio.insert(u"channels"_q, _identity.channels);
	audio.insert(u"frameMs"_q, _identity.frameMs);
	audio.insert(u"bitrate"_q, _identity.bitrate);

	auto hello = QJsonObject();
	hello.insert(u"type"_q, u"hello"_q);
	hello.insert(u"protocol"_q, 1);
	hello.insert(u"callId"_q, _identity.callId);
	hello.insert(u"resume"_q, resume);
	hello.insert(u"employeeNo"_q, _identity.employeeNo);
	hello.insert(u"accountName"_q, _identity.accountName);
	hello.insert(u"peerTgId"_q, qint64(_identity.peerTgId));
	hello.insert(u"direction"_q, _identity.direction);
	hello.insert(u"isVideo"_q, _identity.isVideo);
	hello.insert(u"startedAt"_q, qint64(_identity.startedAt));
	hello.insert(u"clientVersion"_q, _identity.clientVersion);
	hello.insert(u"audio"_q, audio);

	const auto text = QJsonDocument(hello).toJson(QJsonDocument::Compact);
	writeWsFrame(0x1, text);
}

void Uploader::sendFrame(
		int64 tsMs,
		bool gapBefore,
		const QByteArray &payload) {
	if (_state == State::Dead || _finished) {
		return;
	}
	auto frame = Buffered{
		.seq = _nextSeq++,
		.tsMs = tsMs,
		.gapBefore = (gapBefore || _pendingGap),
		.payload = payload,
	};
	_pendingGap = false;
	_buffer.push_back(std::move(frame));
	trimBuffer();

	if (_state == State::Open) {
		const auto &back = _buffer.back();
		if (back.seq >= _sentThrough) {
			writeAudioFrame(back);
			_sentThrough = back.seq + 1;
		}
	}
}

void Uploader::sendBufferedFrom(uint32 seq) {
	while (!_buffer.empty() && _buffer.front().seq < seq) {
		_buffer.pop_front();
	}
	_sentThrough = seq;
	for (const auto &frame : _buffer) {
		writeAudioFrame(frame);
		_sentThrough = frame.seq + 1;
	}
}

void Uploader::writeAudioFrame(const Buffered &frame) {
	auto out = QByteArray();
	out.reserve(kFrameHeaderSize + frame.payload.size());
	out.append(char(kFrameVersion));
	out.append(char(frame.gapBefore ? kFlagGapBefore : 0));
	PutBE32(out, frame.seq);
	PutBE64(out, quint64(frame.tsMs));
	PutBE16(out, quint16(frame.payload.size()));
	out.append(frame.payload);
	writeWsFrame(0x2, out);
}

void Uploader::writeWsFrame(quint8 opcode, const QByteArray &payload) {
	if (!_socket || _state == State::Dead) {
		return;
	}
	auto header = QByteArray();
	header.append(char(0x80 | opcode));
	const auto len = payload.size();
	auto maskFlag = char(0x80);
	if (len < 126) {
		header.append(char(maskFlag | char(len)));
	} else if (len <= 0xFFFF) {
		header.append(char(maskFlag | char(126)));
		PutBE16(header, quint16(len));
	} else {
		header.append(char(maskFlag | char(127)));
		PutBE64(header, quint64(len));
	}
	char mask[4];
	auto *generator = QRandomGenerator::global();
	for (auto i = 0; i != 4; ++i) {
		mask[i] = char(generator->bounded(256));
	}
	header.append(mask, 4);

	auto masked = payload;
	for (auto i = 0; i != masked.size(); ++i) {
		masked[i] = char(masked[i] ^ mask[i % 4]);
	}
	_socket->write(header);
	_socket->write(masked);
}

void Uploader::sendClose(quint16 code) {
	auto payload = QByteArray();
	PutBE16(payload, code);
	writeWsFrame(0x8, payload);
}

void Uploader::finish(int64 endedAt, const QString &reason) {
	if (_finished) {
		return;
	}
	_finished = true;
	_byeEndedAt = endedAt;
	_byeReason = reason;

	if (_state == State::Open && _socket) {
		auto bye = QJsonObject();
		bye.insert(u"type"_q, u"bye"_q);
		bye.insert(u"callId"_q, _identity.callId);
		bye.insert(u"endedAt"_q, qint64(endedAt));
		bye.insert(u"lastSeq"_q, qint64(_nextSeq > 0 ? _nextSeq - 1 : 0));
		bye.insert(u"frameCount"_q, qint64(_nextSeq));
		bye.insert(u"reason"_q, reason);
		writeWsFrame(
			0x1,
			QJsonDocument(bye).toJson(QJsonDocument::Compact));
		sendClose(1000);
		_state = State::Closing;
		_socket->flush();
		_socket->waitForBytesWritten(300);
	}
	die();
}

void Uploader::trimBuffer() {
	while (int(_buffer.size()) > kMaxBuffered) {
		const auto dropped = _buffer.front().seq;
		_buffer.pop_front();
		if (dropped >= _sentThrough) {
			_pendingGap = true;
			if (!_buffer.empty()) {
				_buffer.front().gapBefore = true;
			}
		}
	}
}

void Uploader::onError() {
	if (_finished || _state == State::Dead) {
		return;
	}
	scheduleReconnect();
}

void Uploader::onDisconnected() {
	if (_finished || _state == State::Dead) {
		return;
	}
	scheduleReconnect();
}

void Uploader::scheduleReconnect() {
	if (_finished || _state == State::Dead) {
		return;
	}
	if (_reconnectTimer.isActive()) {
		return;
	}
	_state = State::Idle;
	_reconnectDelay = _reconnectDelay
		? std::min(_reconnectDelay * 2, kReconnectMaxMs)
		: kReconnectStartMs;
	_reconnectTimer.start(_reconnectDelay);
}

void Uploader::die() {
	_state = State::Dead;
	_reconnectTimer.stop();
	if (_socket) {
		_socket->disconnect();
		_socket->abort();
		_socket.reset();
	}
}

} // namespace Calls::Recording

#endif // TDESKTOP_EMPLOYEE_MODE
