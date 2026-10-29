#!/bin/sh
# Builds a bootable PS2 CD/DVD image from an EE ELF: SYSTEM.CNF + RENSGA.ELF.  Run inside tools/docker/ps2.sh (xorriso).
# Usage: tools/docker/ps2.sh tools/ps2/make_iso.sh <game.elf> <out.iso>
# No BIOS is needed to build; booting it is gate F8.G2 (never ship or commit a Sony BIOS).
set -eu
ELF="${1:?elf}"; OUT="${2:?out.iso}"
STAGE="$(mktemp -d)"; trap 'rm -rf "$STAGE"' EXIT
cp "$ELF" "$STAGE/RENSGA.ELF"
printf 'BOOT2 = cdrom0:\\RENSGA.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' > "$STAGE/SYSTEM.CNF"
xorriso -as mkisofs -quiet -iso-level 1 -V RENSGA -o "$OUT" "$STAGE"
echo "wrote $OUT"
