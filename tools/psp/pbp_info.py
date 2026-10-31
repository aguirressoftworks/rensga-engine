#!/usr/bin/env python3
"""Reads an EBOOT.PBP: its sections and the PARAM.SFO entries (F5.03). Stdlib only.

  python3 tools/psp/pbp_info.py EBOOT.PBP [--json] [--extract DIR]

Prints one line per section (name, size), then KEY = value for every PARAM.SFO entry. --extract writes ICON0.PNG, PIC1.PNG, SND0.AT3 and
PARAM.SFO (the ones present) into DIR so they can be looked at. Exit 1 when the file is not a PBP."""
import json, os, struct, sys

SECTIONS = ["PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "UNKNOWN.PNG", "PIC1.PNG", "SND0.AT3", "DATA.PSP", "DATA.PSAR"]


def read_pbp(data):
    if data[:4] != b"\0PBP" or len(data) < 40:
        raise ValueError("not a PBP file")
    offs = list(struct.unpack_from("<8I", data, 8)) + [len(data)]
    out = {}
    for i, name in enumerate(SECTIONS):
        size = offs[i + 1] - offs[i]
        out[name] = data[offs[i]:offs[i] + size] if size > 0 else b""
    return out


def read_sfo(sfo):
    if sfo[:4] != b"\0PSF":
        raise ValueError("PARAM.SFO has no \\0PSF magic")
    key_off, data_off, count = struct.unpack_from("<III", sfo, 8)
    entries = {}
    for i in range(count):
        koff, fmt, length, maxlen, doff = struct.unpack_from("<HHIII", sfo, 20 + i * 16)
        end = sfo.index(b"\0", key_off + koff)
        key = sfo[key_off + koff:end].decode("ascii")
        raw = sfo[data_off + doff:data_off + doff + length]
        if fmt == 0x0404:
            entries[key] = struct.unpack("<I", raw[:4])[0]
        else:
            entries[key] = raw.split(b"\0", 1)[0].decode("utf-8", "replace")
    return entries


def png_size(png):
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", png[16:24])


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    as_json = "--json" in argv
    ext = argv[argv.index("--extract") + 1] if "--extract" in argv else None
    if ext and ext in args:
        args.remove(ext)
    if len(args) != 1:
        print(__doc__.split("\n")[0], file=sys.stderr)
        return 2
    try:
        with open(args[0], "rb") as f:
            secs = read_pbp(f.read())
        sfo = read_sfo(secs["PARAM.SFO"])
    except (OSError, ValueError, struct.error) as e:
        print("pbp_info: %s: %s" % (args[0], e), file=sys.stderr)
        return 1
    info = {"sections": {k: len(v) for k, v in secs.items()}, "sfo": sfo,
            "icon0": png_size(secs["ICON0.PNG"]), "pic1": png_size(secs["PIC1.PNG"])}
    if ext:
        os.makedirs(ext, exist_ok=True)
        for name in ("PARAM.SFO", "ICON0.PNG", "PIC1.PNG", "SND0.AT3"):
            if secs[name]:
                with open(os.path.join(ext, name), "wb") as f:
                    f.write(secs[name])
    if as_json:
        print(json.dumps(info, sort_keys=True))
    else:
        for k, v in info["sections"].items():
            print("section %-12s %d bytes" % (k, v))
        for k in sorted(sfo):
            print("%s = %s" % (k, sfo[k]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
