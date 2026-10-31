#!/usr/bin/env bash
# F5.03-F5.05: a real release package. `rensga build --target psp` on a copy of a PSP template with a Package.json (title, disc ID,
# version) and two music files, then the EBOOT.PBP is read back with tools/psp/pbp_info.py: PARAM.SFO title / DISC_ID / DISC_VERSION /
# APP_VER, ICON0 144x80 and PIC1 480x272 (placeholders from the project name), game.rpak, music, LICENSES/. A second build adds the
# project's own ICON0 / PIC1 / SND0 and checks they replace the placeholders byte for byte.
# usage: package_check.sh <rensga CLI> <template dir> <work dir>.  Exit 77 = no psp-build module (rensga modules install psp-build).
set -eu
CLI=$1; TEMPLATE=$2; WORK=$3
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
[ -x "$CLI" ] || { echo "skip: no rensga CLI"; exit 77; }
command -v python3 >/dev/null || { echo "skip: python3"; exit 77; }
"$CLI" modules path psp-build >/dev/null 2>&1 || { echo "skip: psp-build module not installed (rensga modules install psp-build)"; exit 77; }
INFO=(python3 -I "$REPO/tools/psp/pbp_info.py")
rm -rf "$WORK"; mkdir -p "$WORK/proj"
cp -r "$TEMPLATE/Assets" "$TEMPLATE/ProjectSettings" "$WORK/proj/"
P="$WORK/proj"; OUT="$P/Library/Build/psp-release"
fail() { echo "FAIL: $1" >&2; exit 1; }
cat > "$P/ProjectSettings/Package/Package.json" <<'JSON'
{"format":1,"title":"Pequi Quest","discId":"PEQU00042","version":"1.5"}
JSON
python3 - "$P" <<'PY'
import struct, sys, os
d = os.path.join(sys.argv[1], "Streaming"); os.makedirs(d, exist_ok=True)
for n in ("music.wav", "music2.wav"):   # 0.1 s of silence, 22050 Hz mono 16 bit
    pcm = b"\0\0" * 2205
    open(os.path.join(d, n), "wb").write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, 22050, 44100, 2, 16) + b"data" + struct.pack("<I", len(pcm)) + pcm)
PY
echo "== release build 1 (placeholder art)"
RENSGA_PSP_EXIT_AFTER_FRAMES=0 "$CLI" build --target psp "$P" || fail "rensga build"
[ -s "$OUT/EBOOT.PBP" ] || fail "no EBOOT.PBP"
"${INFO[@]}" "$OUT/EBOOT.PBP" --extract "$WORK/x1" | tee "$WORK/info1.txt"
for kv in "TITLE = Pequi Quest" "DISC_ID = PEQU00042" "DISC_VERSION = 1.50" "APP_VER = 01.50"; do
  grep -qx "$kv" "$WORK/info1.txt" || fail "PARAM.SFO lacks '$kv'"
done
J="$("${INFO[@]}" "$OUT/EBOOT.PBP" --json)"
echo "$J" | grep -q '"icon0": \[144, 80\]' || fail "ICON0 is not 144x80: $J"
echo "$J" | grep -q '"pic1": \[480, 272\]' || fail "PIC1 is not 480x272: $J"
for f in game.rpak music.wav music2.wav LICENSES/RENSGA-MIT.txt LICENSES/README-LICENSES.txt LICENSES/PSPSDK.txt LICENSES/SDL2-zlib.txt; do
  [ -s "$OUT/$f" ] || fail "package lacks $f"
done
cmp -s "$WORK/x1/ICON0.PNG" "$OUT/package/ICON0.png" || fail "the PBP's ICON0 is not the generated one"
echo "== release build 2 (the project's own art)"
python3 -I "$REPO/tools/psp/make_package_art.py" --title "Own Art" --out "$WORK/own"
cp "$WORK/own/ICON0.png" "$WORK/own/PIC1.png" "$P/ProjectSettings/Package/"
printf 'ATRAC3-bytes-not-really' > "$P/ProjectSettings/Package/SND0.AT3"
RENSGA_PSP_EXIT_AFTER_FRAMES=0 "$CLI" build --target psp "$P" || fail "rensga build (own art)"
"${INFO[@]}" "$OUT/EBOOT.PBP" --extract "$WORK/x2" >/dev/null
cmp -s "$WORK/x2/ICON0.PNG" "$WORK/own/ICON0.png" || fail "own ICON0 was not used"
cmp -s "$WORK/x2/PIC1.PNG" "$WORK/own/PIC1.png" || fail "own PIC1 was not used"
[ "$(wc -c < "$WORK/x2/SND0.AT3")" = "$(wc -c < "$P/ProjectSettings/Package/SND0.AT3")" ] || fail "SND0.AT3 is not in the PBP"
echo "psp package: PARAM.SFO + ICON0 + PIC1 + music + LICENSES verified in $OUT"
