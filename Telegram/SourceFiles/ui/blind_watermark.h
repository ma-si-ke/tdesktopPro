/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <vector>

class QImage;

namespace Ui::BlindWatermark {

// Invisible DWT-DCT-SVD watermark, bit-compatible with the Python
// library github.com/guofei9987/blind_watermark (v0.4.4, default
// "common" mode, 4x4 blocks, d1 = 36, d2 = 20). An image produced here
// can be read back with that library given the same two passwords:
//
//   WaterMark(password_img=..., password_wm=...).extract(
//       filename=..., wm_shape=len(bits), mode='bit')
//
// Every bit is repeated over all 4x4 blocks of the low-frequency band of
// all three YUV channels, so the payload survives JPEG re-encoding and
// mild edits. Scaling or cropping the image breaks the block alignment.

// Splits bytes into bits, most significant bit first.
[[nodiscard]] std::vector<uchar> BytesToBits(const QByteArray &bytes);

// Embeds `bits` (each element 0 or 1) into `image` in place. Returns
// false, leaving the image untouched, when the image holds fewer 4x4
// blocks than there are bits (the library requires wm_size < block_num).
bool Embed(
	QImage &image,
	const std::vector<uchar> &bits,
	uint32 passwordImage,
	uint32 passwordWatermark);

} // namespace Ui::BlindWatermark
