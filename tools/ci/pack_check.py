#!/usr/bin/env python3
"""F4.01: refuse a pack the PS2 (GS) and desktop (GL) renderers cannot draw.

Usage: pack_check.py <game.rpak>
A build-only CI job (ps2.sh) cannot see a failed texture upload at run time, so the pack is checked when the ISO is made:
  * no TEXR record may carry flags (kTexSwizzled is PSP-only; GS and GL return 0 from uploadTexture),
  * no TEXR record may use RGBA5551 / RGBA4444 (PSP-only formats),
  * no MSHX section (PSP compact meshes).
Exit 0 ok, 1 refused, 2 unreadable.
"""
import struct
import sys


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    data = open(args[0], "rb").read()
    if len(data) < 32 or data[:4] != b"RPAK":
        print("pack_check: %s is not an RPAK pack" % args[0], file=sys.stderr)
        return 2
    count = struct.unpack_from("<I", data, 8)[0]
    secs = {}
    for i in range(count):
        fourcc, off, size, n = struct.unpack_from("<4sIII", data, 32 + 16 * i)
        secs[fourcc.decode("ascii", "replace")] = (off, size, n)
    bad = []
    if "MSHX" in secs:
        bad.append("MSHX section (PSP compact meshes)")
    if "TEXR" in secs:
        off, size, n = secs["TEXR"]
        for i in range(n):
            # Guid(16) width height format pixelOffset pixelBytes paletteOffset | u16 paletteCount flags storedW storedH
            w, h, fmt = struct.unpack_from("<III", data, off + 48 * i + 16)
            pal, flags, sw, sh = struct.unpack_from("<HHHH", data, off + 48 * i + 40)
            if flags:
                bad.append("texture %d (%dx%d): flags 0x%x (swizzled)" % (i, w, h, flags))
            if fmt > 2:
                bad.append("texture %d (%dx%d): format %d is PSP-only" % (i, w, h, fmt))
        print("pack_check: %s: %d textures, %d sections" % (args[0], n, count))
    if bad:
        print("pack_check: this pack cannot be drawn by the PS2/desktop renderers:", file=sys.stderr)
        for b in bad:
            print("  " + b, file=sys.stderr)
        return 1
    print("pack_check: ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
