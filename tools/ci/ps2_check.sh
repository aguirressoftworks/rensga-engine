#!/usr/bin/env bash
# LOCAL ONLY (never in public CI: PCSX2 needs the user's own BIOS, referenced in place via ~/.config/rensga/emulators.json).
# F4.01 / F4.G2 (PS2): builds the ISO of the 2D slice (samples/platformer2d, tools/ci/ps2.sh unless SKIP_BUILD=1) with the TitleBot
# autostart, a formatted blank card and the save self-test, boots it in PCSX2 under Xvfb and requires in the captured EE console log:
#   * "rensga: game complete" (the bot played the three rooms to the results scene) and "rensga: bot finished the game",
#   * "rensga: prof" lines (frame profiler),
#   * the memory card: "memory card icon: icon.sys ok", "memory card ready" and "save selftest ... wrote=1 readback=<n>" (a save written
#     to mc0:/RENSGA and read back; the bot's own room saves go through the same path),
#   * "rensga: alloc-check frames=<N> allocs=0",
#   * NO texture upload failure ("upload failed", "ERROR [gs]"): a pack the GS cannot draw must not pass as a quiet run.
# It saves a screenshot (Results scene) to $CI_OUT/ps2-check/ps2_slice.png.
# Exit: 0 ok, 1 failed, 77 skipped (no pcsx2-qt / no BIOS config / no ps2-build module).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
"$REPO/tools/emulators/pcsx2_bin.sh" >/dev/null || { echo "ps2_check: PCSX2 not installed (rensga modules install pcsx2), skipped"; exit 77; }
CFGJ="$HOME/.config/rensga/emulators.json"
[ -f "$CFGJ" ] || ls "$HOME/.config/PCSX2/bios/"*.bin >/dev/null 2>&1 || { echo "ps2_check: no PS2 BIOS configured, skipped"; exit 77; }
OUT="$CI_OUT/ps2-check"; mkdir -p "$OUT"
CHECK_FRAMES="${PS2_CHECK_FRAMES:-3000}"
export RENSGA_PS2_BUILD="$OUT"
if [ "${SKIP_BUILD:-0}" != 1 ]; then
  RENSGA_PS2_CMAKE_ARGS="-DRENSGA_PS2_AUTOSTART=TitleBot -DRENSGA_PS2_FORMAT_BLANK_CARD=ON -DRENSGA_PS2_SAVE_SELFTEST=ON -DRENSGA_PS2_CHECK_FRAMES=$CHECK_FRAMES" \
    "$REPO/tools/ci/ps2.sh"; rc=$?
  [ $rc = 0 ] || { [ $rc = 77 ] && exit 77; echo "ps2_check: ISO build failed" >&2; exit 1; }
fi
ISO="$OUT/rensga.iso"; LOG="$OUT/pcsx2.log"; SHOT="$OUT/ps2_slice.png"
# Own PCSX2 sandbox: a parallel run (another agent, another job) must not share build/pcsx2.
export RENSGA_PCSX2_WORK="$OUT/pcsx2"
"$REPO/tools/emulators/pcsx2_boot.sh" "$ISO" --frames "$((CHECK_FRAMES + 60))" --timeout "${PS2_CHECK_TIMEOUT:-900}" --log "$LOG" --shot "$SHOT" \
  || { echo "ps2_check: boot failed (see $LOG)" >&2; exit 1; }
fail=0
need() {  # need <label> <egrep pattern>
  local l; l="$(grep -a -E "$2" "$LOG" | tail -1)"
  if [ -n "$l" ]; then echo "ps2: $1: ${l:0:170}"; else echo "ps2_check: MISSING $1 (/$2/)" >&2; fail=1; fi
}
need "bot completes the game"   'rensga: game complete coins=[0-9]+'
need "bot finished"             'rensga: bot finished the game'
need "profiler"                 'rensga: prof'
need "card icon"                'memory card icon: icon.sys ok, rensga.ico ok'
need "card ready"               'memory card ready'
need "card save + readback"     'rensga: save selftest previous=-?[0-9]+ wrote=1 readback=[1-9][0-9]+'
line="$(grep -a -oE 'rensga: alloc-check frames=[0-9]+ allocs=[0-9]+' "$LOG" | tail -1)"
echo "ps2: ${line:-<no alloc-check line>}"
echo "$line" | grep -qE 'frames=[1-9][0-9]* allocs=0$' || { echo "ps2_check: alloc-check is not allocs=0" >&2; fail=1; }
if grep -a -E 'upload failed|uploadTexture:|ERROR \[gs\]' "$LOG" >/dev/null; then
  echo "ps2_check: a texture upload failed on the GS (nothing, or solid rectangles, is drawn):" >&2
  grep -a -E 'upload failed|uploadTexture:|ERROR \[gs\]' "$LOG" | head -5 >&2; fail=1
fi
[ -s "$SHOT" ] && echo "ps2: screenshot $SHOT" || echo "ps2_check: no screenshot" >&2
[ $fail = 0 ] && echo "ps2_check: ok" || echo "ps2_check: FAILED" >&2
exit $fail
