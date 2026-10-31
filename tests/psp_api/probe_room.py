#!/usr/bin/env python3
"""F5.01 pixel probes for tools/ci/psp_3d.sh (stdlib only). Input: the 24-bit BMPs of four frame dumps of scene RoomBot in time order, then (optional) the fog-on and fog-off dumps.
Prints one line per probe and "probes: OK" or "probes: FAIL". The regions are in the 480x272 frame.

  lit       the colored point lights (red, green, blue) tint the picture: red-minus-green varies across it by at least 10
  shadow    lit and unlit surfaces differ: the brightest and darkest floor patches differ in luminance by at least 25%
  motion    the character moves: the middle of the frame changes between the dumps, and the picture is not the same twice
  fog       dumps 5 and 6 are a few frames apart with the fog switched off in between (RoomRig fogToggleFrame): the far band gets brighter
  frames    no dump is a single color
"""
import struct, sys


def load(path):
    """24-bit BMP -> (w, h, rows of RGB bytes, top row first)."""
    d = open(path, "rb").read()
    if d[:2] != b"BM":
        raise SystemExit("%s: not a BMP" % path)
    off, = struct.unpack_from("<I", d, 10)
    w, h, _planes, bpp = struct.unpack_from("<iiHH", d, 18)
    if bpp != 24:
        raise SystemExit("%s: only 24-bit BMP" % path)
    flip = h > 0
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    rows = []
    for y in range(h):
        sy = h - 1 - y if flip else y
        row = d[off + sy * stride: off + sy * stride + w * 3]
        rows.append(b"".join(bytes((row[i + 2], row[i + 1], row[i])) for i in range(0, w * 3, 3)))  # BGR -> RGB
    return w, h, rows


def px(rows, x, y):
    r = rows[y]
    return r[3 * x], r[3 * x + 1], r[3 * x + 2]


def mean(rows, x0, y0, x1, y1):
    n = 0
    s = [0, 0, 0]
    for y in range(y0, y1):
        for x in range(x0, x1):
            p = px(rows, x, y)
            for i in range(3):
                s[i] += p[i]
            n += 1
    return [v / n for v in s]


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def contrast(rows, y0, y1, x0=0, x1=480):
    ls = [lum(px(rows, x, y)) for y in range(y0, y1, 2) for x in range(x0, x1, 3)]
    ls.sort()
    k = max(1, len(ls) // 10)
    return sum(ls[-k:]) / k - sum(ls[:k]) / k


def diff_share(a, b, x0, y0, x1, y1, thr=24):
    n = d = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            p, q = px(a, x, y), px(b, x, y)
            if abs(p[0] - q[0]) + abs(p[1] - q[1]) + abs(p[2] - q[2]) > thr:
                d += 1
            n += 1
    return d / n


def main():
    frames = [load(p) for p in sys.argv[1:]]
    ok = True
    for i, (w, h, rows) in enumerate(frames):
        colors = len({rows[y][x:x + 3] for y in range(0, h, 4) for x in range(0, w * 3, 12)})
        print("frames: dump %d %dx%d distinct colors (sampled) %d" % (i, w, h, colors))
        if colors < 20:
            ok = False
    w, h, rows = frames[1]
    # lit: the point lights are red, green and blue, so the red-minus-green difference varies across the picture by more than the
    # texture alone would give (a white-lit room: range < 6)
    for i in range(4):
        rows_i = frames[i][2]
        ch = []
        for y in range(60, 242, 30):
            for x in range(0, 440, 40):
                m = mean(rows_i, x, y, x + 40, y + 30)
                ch.append(m[0] - m[1])
        rng = max(ch) - min(ch)
        print("lit: dump %d red-minus-green range over 6x7 patches = %.0f" % (i, rng))
        if rng < 10:
            print("lit: FAIL")
            ok = False
    patches = [mean(rows, x, y, x + 24, y + 12) for y in range(200, 262, 12) for x in range(0, 456, 24)]
    ls = sorted(lum(p) for p in patches)
    spread = (ls[-1] - ls[0]) / max(1.0, ls[-1])
    print("shadow: floor patch luminance min=%.0f max=%.0f spread=%.2f" % (ls[0], ls[-1], spread))
    if spread < 0.25:
        print("shadow: FAIL")
        ok = False
    shares = [diff_share(frames[i][2], frames[i + 1][2], 160, 60, 320, 230) for i in range(3)]
    print("motion: changed share of the middle between dumps %s" % ["%.3f" % s for s in shares])
    if min(shares) < 0.01:
        print("motion: FAIL (the character should move between every two dumps)")
        ok = False
    if len(frames) >= 6:
        # fog: the script switches RenderSettings.fogEnabled off between the last two dumps (a few frames apart). The far part of the
        # picture (the walls and the far floor, rows 40-100) must get brighter and less blue-dark when fog is off.
        on, off = frames[4][2], frames[5][2]
        lon, loff = lum(mean(on, 0, 40, 480, 100)), lum(mean(off, 0, 40, 480, 100))
        print("fog: far band luminance with fog %.0f, without %.0f (change %+.0f%%)" % (lon, loff, 100.0 * (loff - lon) / max(1.0, lon)))
        if loff < lon * 1.08:
            print("fog: FAIL (switching fog off should brighten the far band by at least 8%)")
            ok = False
    print("probes: OK" if ok else "probes: FAIL")
    return 0 if ok else 1


sys.exit(main())
