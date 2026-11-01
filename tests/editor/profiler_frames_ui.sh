#!/bin/bash
# F6.06 exercise (Editor 12) in the real editor: samples/frame_spike on a PSP development build (PPSSPPHeadless), the Profiler window's Frames
# tab finds the spike, Build Settings moves the fixed scene up, the second run shows the difference.
#   profiler_frames_ui.sh <rensga-editor> <frame_spike project> <work dir>      Exit 77 = no xvfb / psp-build module / PPSSPP.
# Prints "frame-spike-ui: before ..." and "frame-spike-ui: after ..." lines (numbers are PPSSPP emulator time) and writes
# <work>/profiler-before.png and profiler-after.png (the Profiler window, floating, the slowest frame selected).
EDITOR=$1; PROJ=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
CLI="$(dirname "$EDITOR")/../../tools/cli/rensga"
export RENSGA_HOME=${RENSGA_HOME:-$(cd "$(dirname "$EDITOR")/../.." && pwd)/../rensga-home}
[ -x "$CLI" ] && "$CLI" modules path psp-build >/dev/null 2>&1 || { echo "skip: psp-build module not installed"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK/home/.config/rensga"
cp -r "$PROJ" "$WORK/proj" && rm -rf "$WORK/proj/Library" "$WORK/proj/Builds"
LOG="$WORK/editor.log"
fail() { echo "FAIL: $1"; tail -40 "$LOG" 2>/dev/null; exit 1; }
export RENSGA_TOOLCHAIN=${RENSGA_TOOLCHAIN:-native} RENSGA_JOBS=${RENSGA_JOBS:-2} RENSGA_PLAY_HEADLESS=1
EV=(playconsole "consolewait:finished|1200" profilerfloat profilerframes profilerselect:worst profilerframedump "shot:$WORK/profiler-before.png"
    "bsscene:up|Smooth" bsscenes profilerselect:latest playconsole "consolewait:finished|600" profilerframes profilerselect:worst profilerframedump "shot:$WORK/profiler-after.png" quit)
args=(); for e in "${EV[@]}"; do args+=(--ui-event "$e"); done
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR" --project "$WORK/proj" --no-splash --no-auto-rebuild --config-dir "$WORK/home/.config/rensga" --quit-after 2000 "${args[@]}" > "$LOG" 2>&1 || fail "editor exit $?"
grep -a -E "^profiler:|^buildsettings: scenes" "$LOG" | cut -c1-260 | sed 's/^/  /'
mapfile -t F < <(grep -a "^profiler: frames=" "$LOG")
[ "${#F[@]}" -ge 2 ] || fail "expected two 'profiler: frames=' lines"
num() { echo "$1" | sed -n "s/.*$2=\([0-9.]*\).*/\1/p"; }
B_MED=$(num "${F[0]}" median_ms); B_MAX=$(num "${F[0]}" worst_ms); A_MED=$(num "${F[1]}" median_ms); A_MAX=$(num "${F[1]}" worst_ms)
echo "frame-spike-ui: before (Spiky)  median=${B_MED} ms worst=${B_MAX} ms"
echo "frame-spike-ui: after  (Smooth) median=${A_MED} ms worst=${A_MAX} ms"
grep -a -q "^profiler: sample PlanRoute" "$LOG" || fail "the slowest frame does not list the PlanRoute sample"
python3 -I -c "
import sys
bm, bx, am, ax = map(float, sys.argv[1:5])
assert bx > 2.0 * bm, 'the Spiky run has no spike (worst %.2f vs median %.2f)' % (bx, bm)
assert ax < 1.5 * am, 'the Smooth run still has a spike (worst %.2f vs median %.2f)' % (ax, am)
" "$B_MED" "$B_MAX" "$A_MED" "$A_MAX" || fail "spike check"
echo "frame-spike-ui: ok, screenshots $WORK/profiler-before.png $WORK/profiler-after.png"
