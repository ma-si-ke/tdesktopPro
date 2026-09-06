/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/history_screenshot_watermark.h"

#include "base/unixtime.h"
#include "data/data_user.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/blind_watermark.h"

#include <QtGui/QImage>

namespace HistoryView {
namespace {

// Payload layout (44 bytes, big-endian), keep in sync with
// docs/tools/screenshot_watermark_extract.py:
//   u8  version = 1
//   u8  backend        (Intro::Employee::BackendType value, 255 = unknown)
//   u32 unixTimeUtc
//   u8[12] employeeId  (UTF-8, zero padded)
//   u8[24] accountName (UTF-8, zero padded, cut on a character boundary)
//   u16 crc16          (CRC-16/CCITT-FALSE over the 42 bytes above)
constexpr auto kVersion = uchar(1);
constexpr auto kUnknownBackend = uchar(255);
constexpr auto kEmployeeIdSize = 12;
constexpr auto kAccountNameSize = 24;

// Both must stay below 2^32 (numpy seed range) and match the script.
constexpr auto kPasswordImage = uint32(1516472801);
constexpr auto kPasswordWatermark = uint32(731205498);

[[nodiscard]] uint16 Crc16(const QByteArray &data) {
	auto crc = uint16(0xFFFF);
	for (const auto byte : data) {
		crc ^= uint16(uchar(byte)) << 8;
		for (auto k = 0; k != 8; ++k) {
			crc = (crc & 0x8000)
				? uint16((crc << 1) ^ 0x1021)
				: uint16(crc << 1);
		}
	}
	return crc;
}

[[nodiscard]] QByteArray FixedUtf8(const QString &text, int size) {
	auto bytes = text.toUtf8();
	if (bytes.size() > size) {
		auto cut = size;
		// Don't leave a multi-byte character half-cut.
		while (cut > 0 && (uchar(bytes.at(cut)) & 0xC0) == 0x80) {
			--cut;
		}
		bytes = bytes.left(cut);
	}
	bytes.append(size - bytes.size(), '\0');
	return bytes;
}

} // namespace

QByteArray BuildScreenshotWatermarkPayload(
		not_null<Main::Session*> session) {
	auto backend = kUnknownBackend;
	auto employeeId = QString();
#ifdef TDESKTOP_EMPLOYEE_MODE
	backend = static_cast<uchar>(session->account().employeeBackend());
	employeeId = session->account().employeeId();
#endif // TDESKTOP_EMPLOYEE_MODE
	const auto now = uint32(base::unixtime::now());

	auto body = QByteArray();
	body.append(char(kVersion));
	body.append(char(backend));
	body.append(char((now >> 24) & 0xFF));
	body.append(char((now >> 16) & 0xFF));
	body.append(char((now >> 8) & 0xFF));
	body.append(char(now & 0xFF));
	body.append(FixedUtf8(employeeId, kEmployeeIdSize));
	body.append(FixedUtf8(session->user()->name(), kAccountNameSize));

	const auto crc = Crc16(body);
	body.append(char((crc >> 8) & 0xFF));
	body.append(char(crc & 0xFF));
	return body;
}

void ApplyScreenshotWatermark(
		QImage &image,
		not_null<Main::Session*> session) {
	const auto payload = BuildScreenshotWatermarkPayload(session);
	const auto embedded = Ui::BlindWatermark::Embed(
		image,
		Ui::BlindWatermark::BytesToBits(payload),
		kPasswordImage,
		kPasswordWatermark);
	if (!embedded) {
		LOG(("Screenshot: image %1x%2 too small for the watermark."
			).arg(image.width()
			).arg(image.height()));
	}
}

} // namespace HistoryView
