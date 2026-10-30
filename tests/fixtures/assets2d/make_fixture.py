#!/usr/bin/env python3
"""Regenerates the binary assets of this fixture (tileset.png, noisy.png, sfx.wav). Run from anywhere."""
import os, struct, wave, zlib
here = os.path.dirname(os.path.abspath(__file__))

def png(path, w, h, px):
    raw = b"".join(b"\x00" + bytes(c for p in px[y*w:(y+1)*w] for c in p) for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

# Tileset: 4 x 2 tiles of 16x16, five colors (fits CLUT4).
cols = [(0, 0, 0, 0), (200, 40, 40, 255), (40, 160, 60, 255), (60, 80, 200, 255), (230, 200, 40, 255)]
w, h = 64, 32
px = []
for y in range(h):
    for x in range(w):
        t = (x // 16) + 4 * (y // 16)
        c = cols[t % 5]
        if (x % 16 in (0, 15)) or (y % 16 in (0, 15)):
            c = cols[(t + 1) % 5] if t % 5 else cols[4]
        px.append(c)
png(os.path.join(here, "Assets/Tiles/tileset.png"), w, h, px)

# Noisy image: 32x32 with ~1000 distinct colors (needs median cut for a 256-color palette).
px = [((x * 8) & 255, (y * 8) & 255, ((x * 31 + y * 17) * 5) & 255, 255) for y in range(32) for x in range(32)]
png(os.path.join(here, "Assets/Tiles/noisy.png"), 32, 32, px)

# Stereo 16-bit WAV, 44100 Hz, 200 frames.
with wave.open(os.path.join(here, "Assets/Audio/sfx.wav"), "wb") as f:
    f.setnchannels(2); f.setsampwidth(2); f.setframerate(44100)
    f.writeframes(b"".join(struct.pack("<hh", (i * 100) % 20000 - 10000, 10000 - (i * 100) % 20000) for i in range(200)))
