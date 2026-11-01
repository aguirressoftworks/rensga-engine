#!/usr/bin/env bash
unset WAYLAND_DISPLAY
# CI / local job build_all (F6.05, gate F6.G2): "Build and Run from Build Settings" for every platform, driven in the REAL editor by --ui-event
# verbs under xvfb (tests/editor/build_settings_ui.sh), then the outputs and the run logs are checked.
#   tools/ci/build_all.sh [platform...]       default: linux windows android psp ps2
#     linux    Builds/linux/<name> + game.rpak; the player ran (alloc-check allocs=0), the editor's Profiler received frames over TCP
#     windows  Builds/windows/<name>.exe + SDL2.dll + game.rpak; run under wine when it is installed (else "built, not run")
#     android  Builds/android/<name>.apk (debug-signed) with libmain.so + assets/game.rpak; "built, not run" without an adb device (phone run deferred)
#     psp      Builds/psp/EBOOT.PBP (release) booted in PPSSPPSDL: 'rensga: frame' heartbeat
#     ps2      Builds/ps2/rensga.iso booted in PCSX2 (LOCAL ONLY: needs your BIOS in ~/.config/rensga/emulators.json; skipped without)
# The PC / phone platforms and PSP use templates/psp-2d; the PS2 ISO uses templates/ps2-2d (one project = one profile).
# Env: RENSGA_EDITOR (default build/host/editor/app/rensga-editor), RENSGA_CI_OUT, RENSGA_BS_QUIT_AFTER, BS_WAIT.
# Output: $RENSGA_CI_OUT/build-all/<platform>/ (editor.log, screenshots) and summary.txt. Exit 0 = all ran ok or skipped (77 per platform is a skip).
set -u
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
EDITOR_BIN="${RENSGA_EDITOR:-$REPO/build/host/editor/app/rensga-editor}"
OUT="$CI_OUT/build-all"; rm -rf "$OUT"; mkdir -p "$OUT"; SUMMARY="$OUT/summary.txt"; : > "$SUMMARY"
PLATFORMS="${*:-linux windows android psp ps2}"
export RENSGA_JOBS="${RENSGA_JOBS:-2}" RENSGA_TOOLCHAIN="${RENSGA_TOOLCHAIN:-native}" RENSGA_BS_QUIT_AFTER="${RENSGA_BS_QUIT_AFTER:-4}"
bad=0
for p in $PLATFORMS; do
  proj="$REPO/templates/psp-2d"; mode=dev
  [ "$p" = ps2 ] && proj="$REPO/templates/ps2-2d"
  [ "$p" = psp ] && mode=release   # the console image a player gets: release EBOOT.PBP
  step "$p ($mode) from $(basename "$proj")"
  "$REPO/tests/editor/build_settings_ui.sh" "$EDITOR_BIN" "$proj" "$OUT/$p" "$p" "$mode" > "$OUT/$p.txt" 2>&1
  rc=$?
  if [ $rc = 0 ]; then echo "$p: ok     $(grep -a '^build-settings-ui:' "$OUT/$p.txt" | sed 's/ buildsettings:.*//')" | tee -a "$SUMMARY"
  elif [ $rc = 77 ]; then echo "$p: SKIPPED $(tail -1 "$OUT/$p.txt")" | tee -a "$SUMMARY"
  else echo "$p: FAILED  (see $OUT/$p.txt)" | tee -a "$SUMMARY"; tail -15 "$OUT/$p.txt"; bad=1; fi
done
echo "build_all: summary in $SUMMARY"
exit $bad
