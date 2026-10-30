#!/usr/bin/env bash
# LOCAL ONLY (never in public CI: PCSX2 needs the user's own BIOS, referenced in place via ~/.config/rensga/emulators.json).
# F2.G1 (PS2): builds the ISO (tools/ci/ps2.sh unless SKIP_BUILD=1), boots it in PCSX2 under Xvfb and requires
# "rensga: alloc-check frames=<N> allocs=0" in the captured EE console log.
# Exit: 0 ok, 1 failed, 77 skipped (no pcsx2-qt / no BIOS config).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
command -v pcsx2-qt >/dev/null || { echo "ps2_check: pcsx2-qt not installed, skipped"; exit 77; }
CFGJ="$HOME/.config/rensga/emulators.json"
[ -f "$CFGJ" ] || ls "$HOME/.config/PCSX2/bios/"*.bin >/dev/null 2>&1 || { echo "ps2_check: no PS2 BIOS configured, skipped"; exit 77; }
[ "${SKIP_BUILD:-0}" = 1 ] || "$REPO/tools/ci/ps2.sh" || exit 1
ISO="$CI_OUT/ps2/rensga.iso"; LOG="$REPO/build/pcsx2/alloc.log"; mkdir -p "$REPO/build/pcsx2"
"$REPO/tools/emulators/pcsx2_boot.sh" "$ISO" --frames 360 --timeout 150 --log "$LOG" || { echo "ps2_check: boot failed" >&2; exit 1; }
line="$(grep -a -oE 'rensga: alloc-check frames=[0-9]+ allocs=[0-9]+' "$LOG" | tail -1)"
echo "ps2: ${line:-<no alloc-check line>}"
echo "$line" | grep -qE 'frames=[1-9][0-9]* allocs=0$'
