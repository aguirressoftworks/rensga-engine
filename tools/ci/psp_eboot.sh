#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# F4.02: the RELEASE flow on PSP. Builds templates/psp-3d with `rensga build --target psp` (RENSGA_PSP_PRX=ON, mksfoex,
# pack-pbp: the same code path as a user's release build) into a private copy of the project, installs the EBOOT.PBP and
# game.rpak on PPSSPP's memory stick (ms0:/PSP/GAME/RENSGA/) and boots the EBOOT.PBP in PPSSPPHeadless.
# The game quits after 240 frames (RENSGA_PSP_EXIT_AFTER_FRAMES, CI only) and logs to host0:/rensga.log.
# Exit 77 when the PSP Build Support module or PPSSPPHeadless / xvfb-run is missing.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
for t in PPSSPPHeadless xvfb-run; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
CLI="$CI_OUT/linux-host/tools/cli/rensga"
if [ ! -x "$CLI" ]; then
  step "host CLI"; cmake --preset linux-host -B "$CI_OUT/linux-host" >/dev/null; cmake --build "$CI_OUT/linux-host" -j"$JOBS" --target rensga
fi
FRAMES=240
GAME_ID="$(grep -o 'RENSGA_PSP_GAME_ID "[^"]*"' engine/backends/platform/psp/include/rensga/platform/psp.h | head -1 | cut -d'"' -f2)"
GAME_ID="${GAME_ID:-RENSGA}"
W="$CI_OUT/psp-eboot"; PROJ="$W/project"; HOME_DIR="$W/home"
rm -rf "$W"; mkdir -p "$PROJ" "$HOME_DIR"
cp -r "$REPO/templates/psp-3d/Assets" "$REPO/templates/psp-3d/ProjectSettings" "$PROJ/"

step "release build (rensga build --target psp)"
RENSGA_PSP_EXIT_AFTER_FRAMES=$FRAMES "$CLI" build --target psp "$PROJ"
OUT="$PROJ/Library/Build/psp-release"
[ -s "$OUT/EBOOT.PBP" ] || { echo "no EBOOT.PBP in $OUT" >&2; exit 1; }
[ -s "$OUT/game.rpak" ] || { echo "no game.rpak in $OUT" >&2; exit 1; }
# EBOOT.PBP starts with the PBP magic "\0PBP"
[ "$(head -c 4 "$OUT/EBOOT.PBP" | od -An -c | tr -d ' ')" = '\0PBP' ] || { echo "EBOOT.PBP has no PBP header" >&2; exit 1; }
ls -l "$OUT/EBOOT.PBP" "$OUT/game.rpak" | awk '{print "  " $5 " bytes  " $9}'

step "headless boot of EBOOT.PBP"
# PPSSPPHeadless keeps its memory stick in $HOME/.ppsspp/PSP (a private HOME isolates the run) and refuses a PBP outside its
# -r directory ("Cannot boot ELF located outside mountRoot"). So: EBOOT.PBP in the -r dir (host0: = where the game writes
# rensga.log), game.rpak where a release build looks for it, ms0:/PSP/GAME/<id>/.
MS="$HOME_DIR/.ppsspp/PSP/GAME/$GAME_ID"; HOST="$W/host"
mkdir -p "$MS" "$HOST"
cp "$OUT/EBOOT.PBP" "$HOST/"
cp "$OUT/game.rpak" "$MS/"
log="$(cd "$W" && HOME="$HOME_DIR" QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 timeout -s KILL 120 xvfb-run -a \
  PPSSPPHeadless "$HOST/EBOOT.PBP" -r "$HOST" -l --graphics=software --timeout=90 2>&1 || true)"
# One PPSSPP version everywhere (F4.02): the emulator that ran must be the one pinned in tools/modules/manifest.d/ppsspp.json.
PIN="$(sed -n 's/.*"version": *"\([^"]*\)".*/\1/p' "$REPO/tools/modules/manifest.d/ppsspp.json" | head -1)"
GOT="$(echo "$log" | sed -n 's/^N PPSSPP v\([0-9.]*\).*/\1/p' | head -1)"
echo "PPSSPPHeadless version: ${GOT:-unknown} (pinned $PIN)"
if [ "$GOT" != "$PIN" ]; then
  [ -n "${CI:-}" ] && { echo "PPSSPP version drift: CI must run the pinned $PIN" >&2; exit 1; }
  echo "warning: local PPSSPPHeadless is not the pinned $PIN"
fi
echo "$log" | grep -E "^(INFO|WARN|ERROR) \[" | tail -15
LOGF="$HOST/rensga.log"
[ -f "$LOGF" ] || { echo "EBOOT did not write host0:/rensga.log (did not boot?)" >&2; exit 1; }
# "scene loaded" is also logged for the built-in test cube, which main_psp falls back to without a pack: so require no fallback.
if grep -qE "no usable|cannot open" "$LOGF"; then
  echo "EBOOT could not read game.rpak from ms0:/PSP/GAME/$GAME_ID (see $LOGF):" >&2; grep -E "no usable|cannot open" "$LOGF" >&2; exit 1
fi
grep -q "rensga: scene loaded" "$LOGF" || { echo "EBOOT never loaded a scene (see $LOGF)" >&2; exit 1; }
grep -qE "rensga: quit after $FRAMES frames" "$LOGF" || { echo "EBOOT did not reach frame $FRAMES (see $LOGF)" >&2; exit 1; }
grep -qE "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$LOGF" || { echo "EBOOT did not log alloc-check allocs=0 (see $LOGF)" >&2; exit 1; }
grep -h "scene loaded\|alloc-check\|quit after" "$LOGF" | tail -3
echo "psp eboot: release EBOOT.PBP booted from ms0:/PSP/GAME/$GAME_ID to frame $FRAMES"
