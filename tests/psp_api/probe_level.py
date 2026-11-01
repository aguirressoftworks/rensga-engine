#!/usr/bin/env python3
"""F6.02 pixel probes for tools/ci/psp_level.sh (stdlib only). Input: the work dir and the comma list of dumped frames; reads
<dir>/raw_<n>.bmp (LevelRawBot, runtime lights) and <dir>/baked_<n>.bmp (LevelBot, baked vertex lighting), 24-bit BMPs of 480x272.

  frames    no dump is a single color
  differ    the baked frame differs from the runtime-lit one at the same frame (mean absolute difference per channel >= 3)
  baked     the baked picture has real light and shadow: its brightest and darkest tenths of the pixels differ by at least 40 levels
  light     the baked picture is not dark: its mean luminance is at least 30
Prints one line per probe and "probes: OK" or "probes: FAIL".
"""
import os, struct, sys


def load(path):
    d = open(path, "rb").read()
    off, = struct.unpack_from("<I", d, 10)
    w, h, _p, bpp = struct.unpack_from("<iiHH", d, 18)
    if bpp != 24:
        raise SystemExit("%s: only 24-bit BMP" % path)
    flip = h > 0
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    px = []
    for y in range(h):
        sy = h - 1 - y if flip else y
        row = d[off + sy * stride: off + sy * stride + w * 3]
        px.append([(row[i + 2], row[i + 1], row[i]) for i in range(0, w * 3, 3)])
    return w, h, px


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def main():
    out, frames = sys.argv[1], sys.argv[2].split(",")
    ok = True
    for n in frames:
        raw = load(os.path.join(out, "raw_%s.bmp" % n))
        baked = load(os.path.join(out, "baked_%s.bmp" % n))
        w, h, _ = raw
        flat = lambda im: [p for row in im[2] for p in row]
        fr, fb = flat(raw), flat(baked)
        for name, f in (("raw", fr), ("baked", fb)):
            if len(set(f[:: max(1, len(f) // 4000)])) < 8:
                print("frames %s %s: FAIL single color" % (n, name)); ok = False
        diff = sum(abs(a[i] - b[i]) for a, b in zip(fr, fb) for i in range(3)) / (3.0 * len(fr))
        ls = sorted(lum(p) for p in fb[:: 3])
        k = max(1, len(ls) // 10)
        spread = sum(ls[-k:]) / k - sum(ls[:k]) / k
        mean = sum(ls) / len(ls)
        g = diff >= 3.0
        b = spread >= 40.0
        l = mean >= 30.0
        print("frame %s: differ=%.1f %s  baked contrast=%.0f %s  baked mean luminance=%.0f %s (raw mean %.0f)" % (
            n, diff, "ok" if g else "FAIL", spread, "ok" if b else "FAIL", mean, "ok" if l else "FAIL", sum(lum(p) for p in fr[:: 3]) / len(fr[:: 3])))
        ok = ok and g and b and l
    print("probes: OK" if ok else "probes: FAIL")


main()
