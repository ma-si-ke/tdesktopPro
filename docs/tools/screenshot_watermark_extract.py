#!/usr/bin/env python3
# coding=utf-8
"""Read the invisible tracing watermark out of a chat screenshot.

The client (Telegram/SourceFiles/history/history_screenshot_watermark.cpp)
embeds a 44-byte record into every screenshot copied with the "截图" button
using the DWT-DCT-SVD scheme of https://github.com/guofei9987/blind_watermark.

Setup (once):
    pip install blind_watermark

Usage:
    python screenshot_watermark_extract.py screenshot.png [more images...]

The image must keep its original pixel size. JPEG re-encoding down to about
quality 75 is fine; scaling or cropping breaks the extraction (resize back
to the original size first if you know it).
"""
import struct
import sys
from datetime import datetime, timezone

# Keep in sync with history_screenshot_watermark.cpp.
PASSWORD_IMAGE = 1516472801
PASSWORD_WATERMARK = 731205498
PAYLOAD_BYTES = 44
BACKENDS = {0: '客服', 1: '员工', 2: '后台', 3: '其他', 255: '未知'}


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def extract(path):
    import blind_watermark
    blind_watermark.bw_notes.close()
    from blind_watermark import WaterMark

    bwm = WaterMark(password_img=PASSWORD_IMAGE, password_wm=PASSWORD_WATERMARK)
    values = bwm.extract(filename=path, wm_shape=PAYLOAD_BYTES * 8, mode='bit')
    bits = [1 if v >= 0.5 else 0 for v in values]
    return bytes(int(''.join(map(str, bits[i:i + 8])), 2) for i in range(0, len(bits), 8))


def describe(payload):
    body, crc = payload[:-2], struct.unpack('>H', payload[-2:])[0]
    version, backend = body[0], body[1]
    timestamp = struct.unpack('>I', body[2:6])[0]
    employee_id = body[6:18].rstrip(b'\0').decode('utf-8', errors='replace')
    account_name = body[18:42].rstrip(b'\0').decode('utf-8', errors='replace')
    when = datetime.fromtimestamp(timestamp, tz=timezone.utc)
    return [
        ('校验', '通过' if crc == crc16(body) else '失败（图片可能被压缩或修改过，以下内容仅供参考）'),
        ('版本', str(version)),
        ('工号', employee_id),
        ('账号名', account_name),
        ('登录服务器', BACKENDS.get(backend, '未知(%d)' % backend)),
        ('时间(UTC)', when.strftime('%Y-%m-%d %H:%M:%S')),
    ]


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    for path in argv[1:]:
        print('==', path)
        try:
            for label, value in describe(extract(path)):
                print('  %s: %s' % (label, value))
        except Exception as error:  # noqa: BLE001 - report and continue
            print('  提取失败: %s' % error)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
