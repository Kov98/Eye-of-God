#!/usr/bin/env python3
"""Builds the GalleryView fixtures.

Everything here is Python standard library only. The EXIF block is assembled by
hand: the APP1 segment, the TIFF header, IFD0, the ExifIFD and the GPSIFD are
written field by field, in both byte orders (II and MM), so the C++ parser is
proven against real bytes and not against a library's output.

The pixel data of the three EXIF-bearing JPEGs comes from base_*.jpg, which
gen_base.cpp renders with Qt: hand-writing a JPEG entropy-coded scan is not what
this oracle is testing, and the image has to actually decode for the thumbnail
grid to be worth photographing. The Android/iOS oracle fixtures in this repo do
not contain a real JPEG (they are random bytes), so there was nothing to reuse.

Outputs, all under .oracle_gallery/:
  fixtures/            the folder the gallery is pointed at
  expected.json        the values the parser must return
"""

import hashlib
import json
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
BASE_DIR = os.path.join(HERE, "base")
FIX_DIR = os.path.join(HERE, "fixtures")
STRESS_DIR = os.path.join(HERE, "stress")

# TIFF field types used below.
TYPE_BYTE = 1
TYPE_ASCII = 2
TYPE_SHORT = 3
TYPE_LONG = 4
TYPE_RATIONAL = 5

# IFD0 / ExifIFD / GPSIFD tags.
TAG_MAKE = 0x010F
TAG_MODEL = 0x0110
TAG_SOFTWARE = 0x0131
TAG_DATETIME = 0x0132
TAG_ORIENTATION = 0x0112
TAG_EXIF_IFD = 0x8769
TAG_GPS_IFD = 0x8825
TAG_DATETIME_ORIGINAL = 0x9003
TAG_DATETIME_DIGITIZED = 0x9004
TAG_PIXEL_X = 0xA002
TAG_PIXEL_Y = 0xA003
TAG_ISO = 0x8827
TAG_EXPOSURE = 0x829A
TAG_FNUMBER = 0x829D
TAG_FOCAL = 0x920A
TAG_GPS_LAT_REF = 0x0001
TAG_GPS_LAT = 0x0002
TAG_GPS_LON_REF = 0x0003
TAG_GPS_LON = 0x0004
TAG_GPS_ALT_REF = 0x0005
TAG_GPS_ALT = 0x0006
TAG_GPS_TIME = 0x0007
TAG_GPS_DATE = 0x001D


# --------------------------------------------------------------------------
# TIFF / APP1 construction
# --------------------------------------------------------------------------
def e_ascii(bo, tag, text):
    payload = text.encode("ascii") + b"\x00"
    return (tag, TYPE_ASCII, len(payload), payload)


def e_short(bo, tag, value):
    return (tag, TYPE_SHORT, 1, struct.pack(bo + "H", value))


def e_long(bo, tag, value):
    return (tag, TYPE_LONG, 1, struct.pack(bo + "I", value))


def e_rational(bo, tag, pairs):
    payload = b"".join(struct.pack(bo + "II", num, den) for num, den in pairs)
    return (tag, TYPE_RATIONAL, len(pairs), payload)


def e_byte(bo, tag, values):
    return (tag, TYPE_BYTE, len(values), bytes(values))


def build_tiff(bo, ifd0, exif_ifd, gps_ifd):
    """Lays out header + IFD0 + ExifIFD + GPSIFD + a value heap. Returns bytes."""
    n0 = len(ifd0) + (1 if exif_ifd else 0) + (1 if gps_ifd else 0)

    def ifd_size(count):
        return 2 + 12 * count + 4

    exif_off = 8 + ifd_size(n0)
    gps_off = exif_off + (ifd_size(len(exif_ifd)) if exif_ifd else 0)
    heap_off = gps_off + (ifd_size(len(gps_ifd)) if gps_ifd else 0)

    entries0 = list(ifd0)
    if exif_ifd:
        entries0.append((TAG_EXIF_IFD, TYPE_LONG, 1, struct.pack(bo + "I", exif_off)))
    if gps_ifd:
        entries0.append((TAG_GPS_IFD, TYPE_LONG, 1, struct.pack(bo + "I", gps_off)))
    entries0.sort(key=lambda entry: entry[0])

    heap = bytearray()

    def emit(entries):
        blob = bytearray(struct.pack(bo + "H", len(entries)))
        for tag, typ, count, payload in entries:
            blob += struct.pack(bo + "HHI", tag, typ, count)
            if len(payload) <= 4:
                blob += payload + b"\x00" * (4 - len(payload))
            else:
                if len(heap) % 2:
                    heap.append(0)
                blob += struct.pack(bo + "I", heap_off + len(heap))
                heap.extend(payload)
        blob += struct.pack(bo + "I", 0)
        return bytes(blob)

    body = emit(entries0)
    if exif_ifd:
        body += emit(sorted(exif_ifd, key=lambda entry: entry[0]))
    if gps_ifd:
        body += emit(sorted(gps_ifd, key=lambda entry: entry[0]))

    order = b"II" if bo == "<" else b"MM"
    return struct.pack(bo + "2sHI", order, 42, 8) + body + bytes(heap)


def app1(tiff, prefix=b"Exif\x00\x00"):
    payload = prefix + tiff
    return b"\xFF\xE1" + struct.pack(">H", len(payload) + 2) + payload


def splice_jpeg(base, *segments):
    """Inserts the segments after SOI (and after APP0, when the encoder wrote one)."""
    if base[:2] != b"\xFF\xD8":
        raise SystemExit("base image is not a JPEG")
    insert = 2
    if base[2:4] == b"\xFF\xE0":
        insert = 4 + struct.unpack(">H", base[4:6])[0]
    return base[:insert] + b"".join(segments) + base[insert:]


def jpeg_size(data):
    """Reads the frame header so the EXIF dimensions can state the truth."""
    i = 2
    while i + 9 < len(data):
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        if marker in (0xD8, 0xD9, 0x01) or 0xD0 <= marker <= 0xD7:
            i += 2
            continue
        seg_len = struct.unpack(">H", data[i + 2:i + 4])[0]
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB):
            height, width = struct.unpack(">HH", data[i + 5:i + 9])
            return width, height
        i += 2 + seg_len
    return 0, 0


# --------------------------------------------------------------------------
# PNG / BMP without any third-party library
# --------------------------------------------------------------------------
def png_chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def png_bytes(width, height, pixel_fn, alpha=False):
    channels = 4 if alpha else 3
    colour_type = 6 if alpha else 2
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0 for the row
        for x in range(width):
            raw.extend(pixel_fn(x, y))
    header = struct.pack(">IIBBBBB", width, height, 8, colour_type, 0, 0, 0)
    assert len(raw) == height * (1 + width * channels)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + png_chunk(b"IEND", b"")
    )


def bmp_bytes(width, height, pixel_fn):
    row_size = (width * 3 + 3) & ~3
    padding = row_size - width * 3
    pixels = bytearray()
    for y in range(height - 1, -1, -1):
        line = bytearray()
        for x in range(width):
            r, g, b = pixel_fn(x, y)[:3]
            line.extend((b, g, r))
        line.extend(b"\x00" * padding)
        pixels.extend(line)
    file_size = 14 + 40 + len(pixels)
    info = struct.pack("<IiiHHIIiiII", 40, width, height, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
    return b"BM" + struct.pack("<IHHI", file_size, 0, 0, 54) + info + bytes(pixels)


# --------------------------------------------------------------------------
# fixture bodies
# --------------------------------------------------------------------------
def exif_a(bo, width, height):
    ifd0 = [
        e_ascii(bo, TAG_MAKE, "Xiaomi"),
        e_ascii(bo, TAG_MODEL, "M2101K6G"),
        e_ascii(bo, TAG_SOFTWARE, "olho-fixture 1.0"),
        e_short(bo, TAG_ORIENTATION, 6),
    ]
    exif_ifd = [
        e_ascii(bo, TAG_DATETIME_ORIGINAL, "2026:01:13 09:41:07"),
        e_ascii(bo, TAG_DATETIME_DIGITIZED, "2026:01:13 09:41:07"),
        e_long(bo, TAG_PIXEL_X, width),
        e_long(bo, TAG_PIXEL_Y, height),
        e_short(bo, TAG_ISO, 320),
        e_rational(bo, TAG_EXPOSURE, [(1, 125)]),
        e_rational(bo, TAG_FNUMBER, [(20, 10)]),
        e_rational(bo, TAG_FOCAL, [(47, 10)]),
    ]
    gps_ifd = [
        e_ascii(bo, TAG_GPS_LAT_REF, "S"),
        e_rational(bo, TAG_GPS_LAT, [(23, 1), (33, 1), (4300, 100)]),
        e_ascii(bo, TAG_GPS_LON_REF, "W"),
        e_rational(bo, TAG_GPS_LON, [(46, 1), (39, 1), (2100, 100)]),
        e_byte(bo, TAG_GPS_ALT_REF, [0]),
        e_rational(bo, TAG_GPS_ALT, [(1521, 2)]),
        e_rational(bo, TAG_GPS_TIME, [(12, 1), (31, 1), (7, 1)]),
        e_ascii(bo, TAG_GPS_DATE, "2026:01:13"),
    ]
    return ifd0, exif_ifd, gps_ifd


def exif_b(bo, width, height):
    ifd0 = [
        e_ascii(bo, TAG_MAKE, "Apple"),
        e_ascii(bo, TAG_MODEL, "iPhone 13"),
        e_ascii(bo, TAG_SOFTWARE, "olho-fixture 1.0"),
        e_ascii(bo, TAG_DATETIME, "2026:01:14 18:05:59"),
        e_short(bo, TAG_ORIENTATION, 1),
    ]
    exif_ifd = [
        e_ascii(bo, TAG_DATETIME_ORIGINAL, "2026:01:14 18:05:59"),
        e_long(bo, TAG_PIXEL_X, width),
        e_long(bo, TAG_PIXEL_Y, height),
        e_short(bo, TAG_ISO, 64),
        e_rational(bo, TAG_EXPOSURE, [(1, 60)]),
        e_rational(bo, TAG_FNUMBER, [(18, 10)]),
        e_rational(bo, TAG_FOCAL, [(52, 10)]),
    ]
    gps_ifd = [
        e_ascii(bo, TAG_GPS_LAT_REF, "N"),
        e_rational(bo, TAG_GPS_LAT, [(48, 1), (51, 1), (2964, 100)]),
        e_ascii(bo, TAG_GPS_LON_REF, "E"),
        e_rational(bo, TAG_GPS_LON, [(2, 1), (17, 1), (4020, 100)]),
        e_byte(bo, TAG_GPS_ALT_REF, [0]),
        e_rational(bo, TAG_GPS_ALT, [(350, 10)]),
    ]
    return ifd0, exif_ifd, gps_ifd


def exif_c(bo, width, height):
    ifd0 = [
        e_ascii(bo, TAG_MAKE, "Samsung"),
        e_ascii(bo, TAG_MODEL, "SM-G991B"),
        e_ascii(bo, TAG_SOFTWARE, "olho-fixture 1.0"),
    ]
    exif_ifd = [
        e_ascii(bo, TAG_DATETIME_ORIGINAL, "2026:01:15 23:59:01"),
        e_long(bo, TAG_PIXEL_X, width),
        e_long(bo, TAG_PIXEL_Y, height),
        e_rational(bo, TAG_EXPOSURE, [(4, 1)]),
        e_rational(bo, TAG_FNUMBER, [(17, 10)]),
    ]
    gps_ifd = [
        e_ascii(bo, TAG_GPS_LAT_REF, "N"),
        e_rational(bo, TAG_GPS_LAT, [(31, 1), (30, 1), (0, 1)]),
        e_ascii(bo, TAG_GPS_LON_REF, "E"),
        e_rational(bo, TAG_GPS_LON, [(35, 1), (30, 1), (0, 1)]),
        e_byte(bo, TAG_GPS_ALT_REF, [1]),
        e_rational(bo, TAG_GPS_ALT, [(4305, 10)]),
    ]
    return ifd0, exif_ifd, gps_ifd


def read_base(name):
    path = os.path.join(BASE_DIR, name)
    if not os.path.exists(path):
        raise SystemExit("missing base image: " + path + " (run gen_base.exe first)")
    with open(path, "rb") as handle:
        return handle.read()


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)


# --------------------------------------------------------------------------
# hostile inputs
# --------------------------------------------------------------------------
def hostile_jpegs(base):
    """Truncated and malformed files that must not crash the parser."""
    out = {}

    good_tiff = build_tiff("<", *exif_a("<", 100, 80))
    good_app1 = app1(good_tiff)

    # APP1 declares 4000 bytes but the file ends after 40 of them.
    out["truncated_app1.jpg"] = b"\xFF\xD8" + b"\xFF\xE1" + struct.pack(">H", 4000) + good_app1[4:44]
    # Cut in the middle of the entropy-coded data: EXIF survives, pixels do not.
    spliced = splice_jpeg(base, good_app1)
    out["truncated_scan.jpg"] = spliced[: len(spliced) // 2]
    # IFD0 offset points past the end of the TIFF block.
    broken = bytearray(good_tiff)
    broken[4:8] = struct.pack("<I", 0x7FFFFFF0)
    out["bad_ifd_offset.jpg"] = splice_jpeg(base, app1(bytes(broken)))
    # IFD0 entry count is 65535: the parser must refuse instead of looping.
    broken = bytearray(good_tiff)
    broken[8:10] = struct.pack("<H", 0xFFFF)
    out["count_bomb.jpg"] = splice_jpeg(base, app1(bytes(broken)))
    # One entry claims 4 GB of ASCII: must be skipped by the bounds check.
    tiny = struct.pack("<2sHI", b"II", 42, 8) + struct.pack("<H", 1)
    tiny += struct.pack("<HHI", TAG_MAKE, TYPE_ASCII, 0xFFFFFFF0)
    tiny += struct.pack("<I", 8 + 14)
    tiny += struct.pack("<I", 0)
    out["size_bomb.jpg"] = splice_jpeg(base, app1(tiny))
    # Not a JPEG at all, and a zero byte file.
    out["garbage.jpg"] = bytes((i * 37 + 11) % 251 for i in range(4096))
    out["empty.jpg"] = b""
    out["soi_only.jpg"] = b"\xFF\xD8"
    # 600 filler segments: the marker walk has to give up cleanly.
    storm = bytearray(b"\xFF\xD8")
    for _ in range(600):
        storm += b"\xFF\xE0" + struct.pack(">H", 3) + b"\x00"
    out["marker_storm.jpg"] = bytes(storm)
    # A run of 0xFF bytes: a fill-byte loop with nothing after it.
    out["fill_storm.jpg"] = b"\xFF\xD8" + b"\xFF" * 4000
    return out


def main():
    if not os.path.isdir(BASE_DIR):
        raise SystemExit("missing " + BASE_DIR + " (run gen_base.exe first)")
    os.makedirs(FIX_DIR, exist_ok=True)

    images_dir = os.path.join(FIX_DIR, "WhatsApp Images")
    video_dir = os.path.join(FIX_DIR, "WhatsApp Video")
    audio_dir = os.path.join(FIX_DIR, "WhatsApp Audio")
    corrupt_dir = os.path.join(FIX_DIR, "corrupt")
    docs_dir = os.path.join(FIX_DIR, "docs")
    for path in (images_dir, video_dir, audio_dir, corrupt_dir, docs_dir):
        os.makedirs(path, exist_ok=True)

    expected = {}

    base1 = read_base("base_1.jpg")
    base2 = read_base("base_2.jpg")
    base3 = read_base("base_3.jpg")
    base4 = read_base("base_4.jpg")

    # --- three JPEGs with real, hand-built EXIF -----------------------------
    specs = [
        ("IMG-20260113-WA0001.jpg", base1, "<", exif_a),
        ("IMG-20260114-WA0002.jpg", base2, ">", exif_b),
        ("IMG-20260115-WA0003.jpg", base3, "<", exif_c),
    ]
    for name, base, bo, builder in specs:
        width, height = jpeg_size(base)
        tiff = build_tiff(bo, *builder(bo, width, height))
        write(os.path.join(images_dir, name), splice_jpeg(base, app1(tiff)))

    width1, height1 = jpeg_size(base1)
    width2, height2 = jpeg_size(base2)
    width3, height3 = jpeg_size(base3)

    expected["IMG-20260113-WA0001.jpg"] = {
        "endian": "II",
        "iso_date": "2026-01-13 09:41:07",
        "Make": "Xiaomi",
        "Model": "M2101K6G",
        "Software": "olho-fixture 1.0",
        "Orientation": "6 (90 graus CW)",
        "DateTimeOriginal": "2026:01:13 09:41:07",
        "DateTimeDigitized": "2026:01:13 09:41:07",
        "PixelXDimension": str(width1),
        "PixelYDimension": str(height1),
        "ISOSpeedRatings": "320",
        "ExposureTime": "1/125 s",
        "FNumber": "f/2.0",
        "FocalLength": "4.7 mm",
        "GPSLatitudeRef": "S",
        "GPSLongitudeRef": "W",
        "GPSLatitude": "-23.561944",
        "GPSLongitude": "-46.655833",
        "GPSAltitude": "760.5 m",
        "GPSDateStamp": "2026:01:13",
        "GPSTimeStamp": "12:31:07",
    }
    expected["IMG-20260114-WA0002.jpg"] = {
        "endian": "MM",
        "iso_date": "2026-01-14 18:05:59",
        "Make": "Apple",
        "Model": "iPhone 13",
        "Software": "olho-fixture 1.0",
        "Orientation": "1 (normal)",
        "DateTimeOriginal": "2026:01:14 18:05:59",
        "DateTime": "2026:01:14 18:05:59",
        "PixelXDimension": str(width2),
        "PixelYDimension": str(height2),
        "ISOSpeedRatings": "64",
        "ExposureTime": "1/60 s",
        "FNumber": "f/1.8",
        "FocalLength": "5.2 mm",
        "GPSLatitudeRef": "N",
        "GPSLongitudeRef": "E",
        "GPSLatitude": "48.858233",
        "GPSLongitude": "2.294500",
        "GPSAltitude": "35.0 m",
    }
    expected["IMG-20260115-WA0003.jpg"] = {
        "endian": "II",
        "iso_date": "2026-01-15 23:59:01",
        "Make": "Samsung",
        "Model": "SM-G991B",
        "Software": "olho-fixture 1.0",
        "DateTimeOriginal": "2026:01:15 23:59:01",
        "PixelXDimension": str(width3),
        "PixelYDimension": str(height3),
        "ExposureTime": "4.00 s",
        "FNumber": "f/1.7",
        "GPSLatitudeRef": "N",
        "GPSLongitudeRef": "E",
        "GPSLatitude": "31.500000",
        "GPSLongitude": "35.500000",
        "GPSAltitude": "-430.5 m",
    }

    # --- same EXIF, but an XMP APP1 comes first -------------------------------
    xmp_payload = b"http://ns.adobe.com/xap/1.0/\x00<x:xmpmeta xmlns:x='adobe:ns:meta/'/>"
    xmp_app1 = b"\xFF\xE1" + struct.pack(">H", len(xmp_payload) + 2) + xmp_payload
    tiff_a_le = build_tiff("<", *exif_a("<", width1, height1))
    with_xmp = splice_jpeg(base1, xmp_app1, app1(tiff_a_le))
    write(os.path.join(images_dir, "IMG-20260113-WA0005.jpg"), with_xmp)
    expected["IMG-20260113-WA0005.jpg"] = dict(expected["IMG-20260113-WA0001.jpg"])
    expected["IMG-20260113-WA0005.jpg"]["iso_date"] = "2026-01-13 09:41:07"

    # --- a plain JPEG with no EXIF at all ------------------------------------
    write(os.path.join(images_dir, "IMG-20260116-WA0006.jpg"), base4)
    expected["IMG-20260116-WA0006.jpg"] = {"present": 0}

    # --- PNG and BMP, encoded by hand ---------------------------------------
    png_rgb = png_bytes(720, 540, lambda x, y: (
        (x * 255) // 719,
        (y * 255) // 539,
        120 + ((x + y) % 120),
    ))
    write(os.path.join(images_dir, "IMG-20260117-WA0007.png"), png_rgb)

    png_rgba = png_bytes(480, 640, lambda x, y: (
        (x * 3) % 256,
        (y * 2) % 256,
        200,
        255 if (x // 40 + y // 40) % 2 == 0 else 96,
    ), alpha=True)
    write(os.path.join(images_dir, "IMG-20260118-WA0008.png"), png_rgba)

    bmp = bmp_bytes(320, 240, lambda x, y: (
        (x * 255) // 319,
        90,
        (y * 255) // 239,
    ))
    write(os.path.join(images_dir, "IMG-20260119-WA0009.bmp"), bmp)

    # --- video / audio / docs placeholders ----------------------------------
    write(os.path.join(video_dir, "VID-20260113-WA0001.mp4"),
          b"\x00\x00\x00\x18ftypmp42\x00\x00\x00\x00mp42isom" + bytes((i * 13 + 7) % 256 for i in range(180000)))
    write(os.path.join(video_dir, "VID-20260114-WA0002.mp4"),
          b"\x00\x00\x00\x18ftypmp42\x00\x00\x00\x00mp42isom" + bytes((i * 29 + 3) % 256 for i in range(95000)))
    write(os.path.join(audio_dir, "AUD-20260113-WA0001.opus"),
          b"OggS\x00\x02\x00\x00" + bytes((i * 7 + 1) % 256 for i in range(24000)))
    write(os.path.join(docs_dir, "IMG-20260120-WA0010.pdf"),
          b"%PDF-1.7\n" + bytes((i * 11 + 5) % 256 for i in range(4000)) + b"\n%%EOF\n")

    # --- hostile files -------------------------------------------------------
    hostile = hostile_jpegs(base2)
    for name, blob in hostile.items():
        write(os.path.join(corrupt_dir, name), blob)

    # --- report.json ---------------------------------------------------------
    report = {
        "tool": "wa_recover",
        "generated": "2026-01-20T11:22:33Z",
        "cases": [
            {
                "jid": "5511987654321@s.whatsapp.net",
                "recovered": [
                    {
                        "path": "WhatsApp Images/IMG-20260113-WA0001.jpg",
                        "jid": "5511987654321@s.whatsapp.net",
                        "message_id": "3EB0A1B2C3D4E5F6A7B8",
                        "timestamp": "2026-01-13T09:41:07Z",
                        "state": "deleted",
                    },
                    {
                        "path": "WhatsApp Images/IMG-20260114-WA0002.jpg",
                        "jid": "5511911112222@s.whatsapp.net",
                        "message_id": "3EB0C9D8E7F6A5B4C3D2",
                        "timestamp": "2026-01-14T18:05:59Z",
                        "state": "deleted",
                    },
                    {
                        "path": "WhatsApp Video/VID-20260113-WA0001.mp4",
                        "jid": "5511987654321@s.whatsapp.net",
                        "message_id": "3EB0111223344556677",
                        "timestamp": "2026-01-13T22:10:44Z",
                        "state": "deleted",
                    },
                ],
            }
        ],
    }
    write(os.path.join(FIX_DIR, "report.json"),
          json.dumps(report, indent=2, sort_keys=True).encode("utf-8"))

    # --- expectations --------------------------------------------------------
    expected_out = {
        "exif": expected,
        "sha256": {name: hashlib.sha256(body).hexdigest() for name, body in
                   [(os.path.basename(path), open(path, "rb").read())
                    for path in sorted(
                        os.path.join(root, name)
                        for root, _dirs, names in os.walk(FIX_DIR)
                        for name in names)]},
    }
    with open(os.path.join(HERE, "expected.json"), "w", encoding="utf-8") as handle:
        json.dump(expected_out, handle, indent=2, sort_keys=True)

    total = sum(len(names) for _root, _dirs, names in os.walk(FIX_DIR))
    print("fixtures: %d files under %s" % (total, FIX_DIR))
    print("expected: %d EXIF expectations, %d sha256 digests" %
          (len(expected), len(expected_out["sha256"])))


def stress(count):
    """N small PNGs, to measure scan/thumbnail behaviour on a big folder."""
    os.makedirs(STRESS_DIR, exist_ok=True)
    for index in range(count):
        width, height = 40, 30
        body = png_bytes(width, height, lambda x, y, seed=index: (
            (x * 2 + seed) % 256,
            (y * 3 + seed) % 256,
            (x + y + seed * 7) % 256,
        ))
        write(os.path.join(STRESS_DIR, "IMG-%05d.png" % index), body)
    print("stress: %d files under %s" % (count, STRESS_DIR))


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "--stress":
        stress(int(sys.argv[2]))
    else:
        main()
