/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

class QImage;

namespace Main {
class Session;
} // namespace Main

namespace HistoryView {

// Invisible tracing watermark for chat screenshots. The payload is a
// fixed 44-byte record (see the .cpp for the layout) holding the employee
// login id, the Telegram account name, the login backend and the UTC
// time of the screenshot. Read it back with
// docs/tools/screenshot_watermark_extract.py.
[[nodiscard]] QByteArray BuildScreenshotWatermarkPayload(
	not_null<Main::Session*> session);

// Embeds the payload into the image in place. Does nothing when the
// image is too small to carry it.
void ApplyScreenshotWatermark(
	QImage &image,
	not_null<Main::Session*> session);

} // namespace HistoryView
