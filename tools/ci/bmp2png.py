#!/usr/bin/env python3
"""F4.07 helper (stdlib only): convert the 24-bit BMPs the PSP GU frame dump writes to PNG, and check that a frame is not blank.
  bmp2png.py in.bmp out.png          convert; prints "<out.png> WxH colors=<distinct colors>"; exit 1 when the picture is a single color
  bmp2png.py --diff a.bmp b.bmp      prints the share of pixels that differ"""
import struct, sys, zlib


def read_bmp(path):
    d = open(path, "rb").read()
    if d[:2] != b"BM":
        raise SystemExit("%s: not a BMP" % path)
    off, = struct.unpack_from("<I", d, 10)
    w, h, planes, bpp = struct.unpack_from("<iiHH", d, 18)
    if bpp != 24:
        raise SystemExit("%s: only 24-bit BMP (got %d)" % (path, bpp))
    flip = h > 0
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(h):
        sy = h - 1 - y if flip else y
        row = d[off + sy * stride: off + sy * stride + w * 3]
        rows.append(b"".join(bytes((row[i + 2], row[i + 1], row[i])) for i in range(0, w * 3, 3)))  # BGR -> RGB
    return w, h, rows


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(t, b):
        c = struct.pack(">I", len(b)) + t + b
        return c + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)

    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
                           chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    a = sys.argv[1:]
    if len(a) == 3 and a[0] == "--diff":
        w1, h1, r1 = read_bmp(a[1])
        w2, h2, r2 = read_bmp(a[2])
        if (w1, h1) != (w2, h2):
            print("size differs")
            return 1
        diff = sum(1 for y in range(h1) for x in range(w1) if r1[y][x * 3:x * 3 + 3] != r2[y][x * 3:x * 3 + 3])
        print("%.2f%% of %d pixels differ" % (100.0 * diff / (w1 * h1), w1 * h1))
        return 0
    if len(a) != 2:
        print(__doc__)
        return 2
    w, h, rows = read_bmp(a[0])
    write_png(a[1], w, h, rows)
    colors = len({r[x * 3:x * 3 + 3] for r in rows for x in range(w)})
    print("%s %dx%d colors=%d" % (a[1], w, h, colors))
    return 0 if colors > 1 else 1


sys.exit(main())
