#!/bin/bash
# F6.05 / gate F6.G2: Build and Run from the Build Settings window of the real editor, driven by --ui-event verbs under xvfb, for ONE platform.
#   build_settings_ui.sh <rensga-editor> <project dir> <work dir> <linux|windows|android|psp|ps2> [dev|release]
# The project is copied to <work>/proj (Library, Builds removed). Steps: open Build Settings, list the platforms (status), select the platform,
# set the development flag, Build and Run, wait until the run ends (or, for PPSSPP, until the game printed its heartbeat, then Stop), print
# the state. Screenshots: <work>/bs-<platform>-window.png (before) and bs-<platform>-after.png (after the run).
# Env: RENSGA_BS_QUIT_AFTER=<s> makes the Linux player and wine end by themselves (default 4); BS_WAIT=<s> upper bound of the build (default 1500).
# Prints one line "build-settings-ui: <platform> output=<path> notrun=<0|1> state=<n>" and exits 0, or FAIL lines and exit 1. Exit 77 = skipped
# (no xvfb, editor, module, emulator, BIOS or wine for that platform).
EDITOR=$1; PROJ=$2; WORK=$3; P=$4; MODE=${5:-dev}
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
CLI="$(dirname "$EDITOR")/../../tools/cli/rensga"
mod() { [ -x "$CLI" ] && "$CLI" modules path "$1" >/dev/null 2>&1; }
case $P in
  linux) ;;
  windows) mod windows-build || { echo "skip: windows-build module not installed"; exit 77; } ;;
  android) mod android-build || { echo "skip: android-build module not installed (needs the Google SDK license, rensga modules install android-build)"; exit 77; } ;;
  psp) mod psp-build || { echo "skip: psp-build module not installed"; exit 77; }
       command -v PPSSPPSDL >/dev/null 2>&1 || [ -n "${RENSGA_PPSSPP:-}" ] || { echo "skip: no PPSSPPSDL"; exit 77; } ;;
  ps2) mod ps2-build || { echo "skip: ps2-build module not installed"; exit 77; }
       [ -f "$HOME/.config/rensga/emulators.json" ] || { echo "skip: no BIOS configured (~/.config/rensga/emulators.json)"; exit 77; } ;;
  *) echo "usage: $0 <editor> <project> <work> <platform> [dev|release]"; exit 2 ;;
esac
rm -rf "$WORK"; mkdir -p "$WORK/home/.config/rensga"
cp -r "$PROJ" "$WORK/proj" && rm -rf "$WORK/proj/Library" "$WORK/proj/Builds"
fail() { echo "FAIL: $1"; tail -50 "${LOG:-/dev/null}" 2>/dev/null; exit 1; }
export RENSGA_TOOLCHAIN=${RENSGA_TOOLCHAIN:-native} RENSGA_JOBS=${RENSGA_JOBS:-2} RENSGA_BS_QUIT_AFTER=${RENSGA_BS_QUIT_AFTER:-4}
export RENSGA_HOME=${RENSGA_HOME:-$(cd "$(dirname "$EDITOR")/../.." && pwd)/../rensga-home}
DEV=1; [ "$MODE" = release ] && DEV=0
WAIT=${BS_WAIT:-1500}
LOG="$WORK/editor.log"
EV=(buildsettings bsplatforms "bsplatform:$P" "bsdev:$DEV" "shot:$WORK/bs-$P-window.png" bsscenes bsrun)
if [ "$P" = psp ]; then   # PPSSPPSDL keeps running: wait for the game's heartbeat, look at it, stop it
  EV+=("bswait:running|$WAIT" "consolewait:log=rensga: frame|120" "shot:$WORK/bs-$P-after.png" bsstate bsstop "bswait:finished|60")
else
  EV+=("bswait:finished|$WAIT" bsstate "shot:$WORK/bs-$P-after.png")
fi
EV+=(profilerframes quit)
args=(); for e in "${EV[@]}"; do args+=(--ui-event "$e"); done
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR" --project "$WORK/proj" --no-splash --no-auto-rebuild --config-dir "$WORK/home/.config/rensga" --quit-after $((WAIT + 300)) \
  "${args[@]}" > "$LOG" 2>&1 || fail "editor exit $?"
grep -E "^buildsettings:" "$LOG" | sed 's/^/  /'
grep -q "buildsettings: wait .* done" "$LOG" || fail "the run never finished"
STATE="$(grep '^buildsettings: state=' "$LOG" | tail -1)"
echo "$STATE" | grep -q "state=3" || [ "$P" = psp ] || fail "Build and Run did not end ok: $STATE"
OUT="$(echo "$STATE" | sed -n 's/.*output=\([^ ]*\).*/\1/p')"
NOTRUN="$(echo "$STATE" | sed -n 's/.*notrun=\([01]\).*/\1/p')"
[ -n "$OUT" ] && [ "$OUT" != "-" ] && [ -e "$OUT" ] || fail "no output was produced ($OUT)"
case $P in
  linux)   [ -x "$OUT/$(ls "$OUT" | grep -v -E '\.|^game' | head -1)" ] || fail "no executable in $OUT"
           grep -q "rensga: alloc-check frames=[0-9]* allocs=0" "$LOG" || fail "player: no 'alloc-check ... allocs=0' line" ;;
  windows) ls "$OUT"/*.exe >/dev/null 2>&1 && [ -f "$OUT/SDL2.dll" ] && [ -f "$OUT/game.rpak" ] || fail "windows folder is incomplete"
           [ "$NOTRUN" = 1 ] || grep -q "rensga: alloc-check frames=[0-9]* allocs=0\|rensga: quit after" "$LOG" || fail "wine run: no end-of-run line" ;;
  android) unzip -l "$OUT" | grep -q "lib/arm64-v8a/libmain.so" && unzip -l "$OUT" | grep -q "assets/game.rpak" || fail "APK lacks libmain.so or game.rpak"
           [ "$NOTRUN" = 1 ] || echo "  android: ran on an attached device" ;;
  psp)     [ "$DEV" = 0 ] && { [ "$(basename "$OUT")" = EBOOT.PBP ] || fail "release psp output is not EBOOT.PBP: $OUT"; }
           grep -q "rensga: frame" "$LOG" || fail "PPSSPP: no game heartbeat" ;;
  ps2)     [ "$(basename "$OUT")" = rensga.iso ] || fail "ps2 output is not rensga.iso: $OUT"
           grep -q "rensga: frame\|PASS\|heartbeat" "$LOG" || echo "  ps2: iso built; PCSX2 heartbeat not seen in the editor log (see $WORK/proj/Library/Logs/pcsx2.log)" ;;
esac
echo "build-settings-ui: $P output=$OUT notrun=${NOTRUN:-0} $STATE"
