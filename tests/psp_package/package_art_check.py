#!/usr/bin/env python3
"""F5.04/F5.05 checks without a toolchain: placeholder art (size, bytes stable, differs per title), pbp_info.py on a synthetic PBP with a
PARAM.SFO, and Package.json of the two PSP templates. usage: package_art_check.py <repo> <work dir>"""
import json, os, struct, subprocess, sys

repo, work = sys.argv[1], sys.argv[2]
os.makedirs(work, exist_ok=True)
sys.path.insert(0, os.path.join(repo, "tools", "psp"))
import pbp_info  # noqa: E402

fails = []
def check(cond, what):
    if not cond:
        fails.append(what)
        print("FAIL:", what)

def art(title, out):
    subprocess.run([sys.executable, "-I", os.path.join(repo, "tools", "psp", "make_package_art.py"), "--title", title, "--out", out], check=True)
    return [open(os.path.join(out, n), "rb").read() for n in ("ICON0.png", "PIC1.png")]

a1 = art("Space Pequi", os.path.join(work, "a1"))
a2 = art("Space Pequi", os.path.join(work, "a2"))
b = art("A Much Longer Title That Cannot Fit On The Icon At Any Size", os.path.join(work, "b"))
check(pbp_info.png_size(a1[0]) == (144, 80), "ICON0 is 144x80")
check(pbp_info.png_size(a1[1]) == (480, 272), "PIC1 is 480x272")
check(a1 == a2, "same title gives the same bytes")
check(a1[0] != b[0] and a1[1] != b[1], "another title gives other art")
check(pbp_info.png_size(b[0]) == (144, 80) and pbp_info.png_size(b[1]) == (480, 272), "long title keeps the sizes")

def sfo_bytes(entries):
    keys = b"".join(k.encode() + b"\0" for k, _ in entries)
    keys += b"\0" * ((-len(keys)) % 4)
    data = b""; table = b""; koff = 0
    for k, v in entries:
        if isinstance(v, int):
            raw, fmt, ln, mx = struct.pack("<I", v), 0x0404, 4, 4
        else:
            raw = v.encode() + b"\0"; mx = (len(raw) + 3) // 4 * 4 + 4; fmt, ln = 0x0204, len(raw)
        table += struct.pack("<HHIII", koff, fmt, ln, mx, len(data))
        data += raw + b"\0" * (mx - len(raw))
        koff += len(k) + 1
    key_off = 20 + len(table)
    return b"\0PSF" + struct.pack("<IIII", 0x101, key_off, key_off + len(keys), len(entries)) + table + keys + data

sfo = sfo_bytes([("APP_VER", "01.00"), ("DISC_ID", "RNSG00042"), ("DISC_VERSION", "1.00"), ("MEMSIZE", 1), ("TITLE", "Space Pequi")])
secs = [sfo, a1[0], b"", b"", a1[1], b"", b"prx", b""]
off = 40; offs = []
for s in secs:
    offs.append(off); off += len(s)
pbp = b"\0PBP" + struct.pack("<I", 0x10000) + struct.pack("<8I", *offs) + b"".join(secs)
path = os.path.join(work, "t.PBP")
open(path, "wb").write(pbp)
r = subprocess.run([sys.executable, "-I", os.path.join(repo, "tools", "psp", "pbp_info.py"), path, "--json", "--extract", os.path.join(work, "x")], capture_output=True, text=True)
check(r.returncode == 0, "pbp_info reads a PBP: " + r.stderr)
info = json.loads(r.stdout)
check(info["sfo"] == {"APP_VER": "01.00", "DISC_ID": "RNSG00042", "DISC_VERSION": "1.00", "MEMSIZE": 1, "TITLE": "Space Pequi"}, "SFO entries read back: %s" % info["sfo"])
check(info["icon0"] == [144, 80] and info["pic1"] == [480, 272], "PBP carries ICON0 and PIC1")
check(info["sections"]["DATA.PSP"] == 3, "DATA.PSP size")
check(open(os.path.join(work, "x", "ICON0.PNG"), "rb").read() == a1[0], "extracted ICON0 equals the input")
open(os.path.join(work, "bad.PBP"), "wb").write(b"not a pbp at all, just text........................")
check(subprocess.run([sys.executable, "-I", os.path.join(repo, "tools", "psp", "pbp_info.py"), os.path.join(work, "bad.PBP")], capture_output=True).returncode == 1, "a non-PBP is refused")

import re
for t in ("psp-3d", "psp-2d"):
    p = os.path.join(repo, "templates", t, "ProjectSettings", "Package", "Package.json")
    check(os.path.exists(p), t + " has ProjectSettings/Package/Package.json")
    if os.path.exists(p):
        j = json.load(open(p))
        check(j.get("format") == 1 and re.fullmatch(r"\d{1,2}\.\d{1,2}", j.get("version", "")), t + " Package.json has a version")
        check("discId" not in j or re.fullmatch(r"[A-Z]{4}\d{5}", j["discId"]), t + " discId is valid or absent")
    check(os.path.exists(os.path.join(repo, "templates", t, "ProjectSettings", "Package", "README.txt")), t + " explains the Package folder")

print("package art: %d failure(s)" % len(fails))
sys.exit(1 if fails else 0)
