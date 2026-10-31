#!/usr/bin/env python3
"""Placeholder package art for a PSP EBOOT.PBP, made from the project name and the brand tokens (F5.04/F5.05).

  python3 tools/psp/make_package_art.py --title "My Game" --out <dir> [--icon0 | --pic1]

Writes <dir>/ICON0.png (144x80) and <dir>/PIC1.png (480x272). The colors come from docs/rensga-engine/brand/palette.json (dark theme
tokens bg, surface, text, text-2, border, primary and the icon colors), the logo is the pequi icon of the brand drawn pixel by pixel,
the text uses a 5x7 pixel font defined below. Stdlib only, no timestamps: the same title gives the same bytes.
A game with its own art puts ICON0.png / PIC1.png (and an optional SND0.AT3) in ProjectSettings/Package/ and this script is not used."""
import argparse, json, math, os, struct, sys, zlib

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.dirname(os.path.dirname(here))

# 5x7 pixel font, rows top to bottom, '#' = pixel. Upper case letters, digits and a few signs.
FONT_SRC = """
A .###. #...# #...# ##### #...# #...# #...#
B ####. #...# #...# ####. #...# #...# ####.
C .#### #.... #.... #.... #.... #.... .####
D ####. #...# #...# #...# #...# #...# ####.
E ##### #.... #.... ####. #.... #.... #####
F ##### #.... #.... ####. #.... #.... #....
G .#### #.... #.... #..## #...# #...# .###.
H #...# #...# #...# ##### #...# #...# #...#
I ##### ..#.. ..#.. ..#.. ..#.. ..#.. #####
J ..### ...#. ...#. ...#. ...#. #..#. .##..
K #...# #..#. #.#.. ##... #.#.. #..#. #...#
L #.... #.... #.... #.... #.... #.... #####
M #...# ##.## #.#.# #...# #...# #...# #...#
N #...# ##..# #.#.# #..## #...# #...# #...#
O .###. #...# #...# #...# #...# #...# .###.
P ####. #...# #...# ####. #.... #.... #....
Q .###. #...# #...# #...# #.#.# #..#. .##.#
R ####. #...# #...# ####. #.#.. #..#. #...#
S .#### #.... #.... .###. ....# ....# ####.
T ##### ..#.. ..#.. ..#.. ..#.. ..#.. ..#..
U #...# #...# #...# #...# #...# #...# .###.
V #...# #...# #...# #...# #...# .#.#. ..#..
W #...# #...# #...# #...# #.#.# ##.## #...#
X #...# #...# .#.#. ..#.. .#.#. #...# #...#
Y #...# #...# .#.#. ..#.. ..#.. ..#.. ..#..
Z ##### ....# ...#. ..#.. .#... #.... #####
0 .###. #...# #..## #.#.# ##..# #...# .###.
1 ..#.. .##.. ..#.. ..#.. ..#.. ..#.. .###.
2 .###. #...# ....# ...#. ..#.. .#... #####
3 .###. #...# ....# ..##. ....# #...# .###.
4 ...#. ..##. .#.#. #..#. ##### ...#. ...#.
5 ##### #.... ####. ....# ....# #...# .###.
6 .###. #.... #.... ####. #...# #...# .###.
7 ##### ....# ...#. ..#.. .#... .#... .#...
8 .###. #...# #...# .###. #...# #...# .###.
9 .###. #...# #...# .#### ....# ....# .###.
- ..... ..... ..... ##### ..... ..... .....
. ..... ..... ..... ..... ..... .##.. .##..
: ..... .##.. .##.. ..... .##.. .##.. .....
! ..#.. ..#.. ..#.. ..#.. ..#.. ..... ..#..
_ ..... ..... ..... ..... ..... ..... #####
' ' ..... ..... ..... ..... ..... ..... .....
"""
FONT = {}
for line in FONT_SRC.strip().splitlines():
    parts = line.split()
    ch, rows = (" ", parts[2:]) if parts[0] == "'" else (parts[0], parts[1:])
    FONT[ch] = rows


def hexrgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def tokens():
    """Dark-theme token colors from the generated palette; hard-coded pequi fallbacks if the file is missing."""
    t = {"bg": "#1c1a16", "surface": "#2b2822", "text": "#faf8f3", "text-2": "#c9c3b4", "border": "#403b31", "primary": "#f2a516"}
    icon = {"Stem": "#5a4326", "Leaf": "#4f7d2a", "Husk": "#5f8f34", "Husk rim": "#3f6420", "Pulp": "#f2a516", "Pulp rim": "#d98a0b", "Highlight": "#ffd66b"}
    try:
        with open(os.path.join(repo, "docs", "rensga-engine", "brand", "palette.json")) as f:
            p = json.load(f)
        for tk in p["tokens"]:
            if tk["name"] in t:
                t[tk["name"]] = tk["dark"]
        for part in p["icon"]:
            icon[part["part"]] = part["hex"]
    except (OSError, KeyError, ValueError):
        pass
    return {k: hexrgb(v) for k, v in t.items()}, {k: hexrgb(v) for k, v in icon.items()}


class Canvas:
    def __init__(self, w, h, fill):
        self.w, self.h = w, h
        self.px = [list(fill) for _ in range(w * h)]

    def blend(self, x, y, c, a=1.0):
        if 0 <= x < self.w and 0 <= y < self.h and a > 0:
            p = self.px[y * self.w + x]
            for i in range(3):
                p[i] = p[i] + (c[i] - p[i]) * a

    def rect(self, x, y, w, h, c):
        for yy in range(max(0, y), min(self.h, y + h)):
            for xx in range(max(0, x), min(self.w, x + w)):
                self.px[yy * self.w + xx] = list(c)

    def shape(self, bbox, inside, c, ss=3):
        """Anti-aliased fill: inside(x, y) is evaluated ss*ss times per pixel inside bbox."""
        x0, y0, x1, y1 = bbox
        for y in range(max(0, int(y0)), min(self.h, int(y1) + 1)):
            for x in range(max(0, int(x0)), min(self.w, int(x1) + 1)):
                n = 0
                for sy in range(ss):
                    for sx in range(ss):
                        if inside(x + (sx + 0.5) / ss, y + (sy + 0.5) / ss):
                            n += 1
                if n:
                    self.blend(x, y, c, n / (ss * ss))

    def circle(self, cx, cy, r, c):
        self.shape((cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1), lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r, c)

    def ring(self, cx, cy, r, wd, c):
        a, b = (r - wd / 2) ** 2, (r + wd / 2) ** 2
        self.shape((cx - r - wd, cy - r - wd, cx + r + wd, cy + r + wd), lambda x, y: a <= (x - cx) ** 2 + (y - cy) ** 2 <= b, c)

    def ellipse(self, cx, cy, rx, ry, c):
        self.shape((cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1), lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1, c)

    def text_width(self, s, scale):
        return len(s) * 6 * scale - scale if s else 0

    def text(self, x, y, s, scale, c):
        for ch in s:
            g = FONT.get(ch) or FONT.get(ch.upper()) or FONT[" "]
            for ry, row in enumerate(g):
                for rx, v in enumerate(row):
                    if v == "#":
                        self.rect(x + rx * scale, y + ry * scale, scale, scale, c)
            x += 6 * scale

    def png(self):
        raw = bytearray()
        for y in range(self.h):
            raw.append(0)
            for x in range(self.w):
                p = self.px[y * self.w + x]
                raw += bytes(max(0, min(255, int(round(v)))) for v in p)

        def chunk(tag, data):
            body = tag + data
            return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", self.w, self.h, 8, 2, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def pequi(cv, cx, cy, size, ic):
    """The brand icon (docs build.ICON, viewBox 64) scaled so that 64 units = size pixels, centered at cx, cy."""
    k = size / 64.0
    ox, oy = cx - size / 2.0, cy - size / 2.0
    def X(v): return ox + v * k
    def Y(v): return oy + v * k
    cv.ellipse(X(45), Y(8.5), 10 * k, 3.4 * k, ic["Leaf"])                  # leaf
    cv.rect(int(X(33.2)), int(Y(6)), max(1, int(2 * k)), max(1, int(9 * k)), ic["Stem"])  # stem
    cv.circle(X(32), Y(37), 23 * k, ic["Husk rim"])
    cv.circle(X(32), Y(37), 21.8 * k, ic["Husk"])
    cv.circle(X(32), Y(38), 15 * k, ic["Pulp rim"])
    cv.circle(X(32), Y(38), 13.8 * k, ic["Pulp"])
    cv.ellipse(X(27), Y(33), 5 * k, 3.5 * k, ic["Highlight"])


def fit_title(cv, title, maxw, scales):
    t = title.upper()
    for s in scales:
        if cv.text_width(t, s) <= maxw:
            return t, s
    s = scales[-1]
    while t and cv.text_width(t + "-", s) > maxw:
        t = t[:-1]
    return (t.rstrip() + "-") if t != title.upper() else t, s


def icon0(title, tk, ic):
    cv = Canvas(144, 80, tk["bg"])
    cv.rect(0, 0, 144, 80, tk["bg"])
    cv.rect(0, 0, 144, 2, tk["primary"])
    cv.rect(0, 78, 144, 2, tk["border"])
    pequi(cv, 30, 40, 44, ic)
    t, s = fit_title(cv, title, 144 - 62 - 6, (3, 2, 1))
    cv.text(58, 26 if s > 1 else 30, t, s, tk["text"])
    cv.text(58, 52, "RENSGA", 1, tk["primary"])
    cv.text(58, 62, "ENGINE", 1, tk["text-2"])
    return cv


def pic1(title, tk, ic):
    cv = Canvas(480, 272, tk["bg"])
    for y in range(272):   # soft vertical gradient bg -> surface
        a = y / 271.0
        row = [tk["bg"][i] + (tk["surface"][i] - tk["bg"][i]) * a for i in range(3)]
        for x in range(480):
            cv.px[y * 480 + x] = list(row)
    cv.rect(0, 0, 480, 4, tk["primary"])
    cv.rect(0, 268, 480, 4, tk["border"])
    pequi(cv, 240, 84, 100, ic)
    t, s = fit_title(cv, title, 440, (6, 5, 4, 3, 2))
    cv.text((480 - cv.text_width(t, s)) // 2, 168, t, s, tk["text"])
    sub = "MADE WITH RENSGA ENGINE"
    cv.text((480 - cv.text_width(sub, 2)) // 2, 232, sub, 2, tk["primary"])
    return cv


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--title", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--icon0", action="store_true", help="only ICON0.png")
    ap.add_argument("--pic1", action="store_true", help="only PIC1.png")
    a = ap.parse_args()
    tk, ic = tokens()
    os.makedirs(a.out, exist_ok=True)
    both = not (a.icon0 or a.pic1)
    if a.icon0 or both:
        with open(os.path.join(a.out, "ICON0.png"), "wb") as f:
            f.write(icon0(a.title, tk, ic).png())
    if a.pic1 or both:
        with open(os.path.join(a.out, "PIC1.png"), "wb") as f:
            f.write(pic1(a.title, tk, ic).png())
    return 0


if __name__ == "__main__":
    sys.exit(main())
