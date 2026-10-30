#!/bin/sh
# Builds a bootable PS2 CD/DVD image from an EE ELF: SYSTEM.CNF + RENSGA.ELF.  Runs on the host with rensga-iso (tools/iso); no xorriso, no Docker.
# Usage: tools/ps2/make_iso.sh <game.elf> <out.iso> [game.rpak [streamed-file ...]]
# Streamed files (Streaming/music.wav copied next to the pack by the cooker) go to the disc root in upper case
# (MUSIC.WAV), where AudioMixer::playMusic reads them in chunks as data:/music.wav.
# The pack goes to the disc root as GAME.RPK (ISO 9660 level 1: 8.3 names), which main_ps2 reads as data:/game.rpk.
# No BIOS is needed to build; booting it is gate F8.G2 (never ship or commit a Sony BIOS).
set -eu
ELF="${1:?elf}"; OUT="${2:?out.iso}"; PACK="${3:-}"
shift 2; [ $# -gt 0 ] && shift
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
DISC="$STAGE/disc"; mkdir "$DISC"
cp "$ELF" "$DISC/RENSGA.ELF"
[ -n "$PACK" ] && cp "$PACK" "$DISC/GAME.RPK"
for f in "$@"; do
  up="$(basename "$f" | tr 'a-z' 'A-Z')"
  cp "$f" "$DISC/$up"
done
printf 'BOOT2 = cdrom0:\\RENSGA.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > "$DISC/SYSTEM.CNF"
# rensga-iso: $RENSGA_ISO, else PATH, else a host build tree, else compiled once from tools/iso with the system C++ compiler.
HERE="$(cd "$(dirname "$0")" && pwd)"; REPO="$(cd "$HERE/../.." && pwd)"
ISO="${RENSGA_ISO:-}"
[ -n "$ISO" ] || ISO="$(command -v rensga-iso 2>/dev/null || true)"
for c in "$REPO/build/host/tools/iso/rensga-iso" "$REPO"/build/*/tools/iso/rensga-iso; do [ -n "$ISO" ] || { [ -x "$c" ] && ISO="$c"; }; done
if [ -z "$ISO" ]; then
  ISO="$STAGE/rensga-iso"
  "${CXX:-c++}" -std=c++17 -O1 -I"$REPO/tools/iso/include" "$REPO/tools/iso/src/iso.cpp" "$REPO/tools/iso/src/main.cpp" -o "$ISO"
fi
ORDER="SYSTEM.CNF,RENSGA.ELF"; [ -n "$PACK" ] && ORDER="$ORDER,GAME.RPK"
"$ISO" -o "$OUT" -V RENSGA --order "$ORDER" "$DISC"
echo "wrote $OUT"
