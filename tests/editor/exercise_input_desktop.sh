#!/bin/bash
# API 5 exercise "input test scene": samples/platformer2d's InputTest scene on the desktop player with injected events (a virtual
# SDL gamepad with sticks, triggers, buttons, a shoulder rumble combo, plus keyboard keys). The sample's scripts are compiled into
# a project build tree (incremental when <sample build dir> already exists), the ps2 pack is cooked into a temp copy.
# usage: exercise_input_desktop.sh <source dir> <sample build dir> <samples/platformer2d> <work dir>. Exit 77 = skipped.
SRC="$1"; B="$2"; SAMPLE="$3"; WORK="$4"
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
export RENSGA_JOBS=${RENSGA_JOBS:-2}
fail() { echo "FAIL: $1"; tail -30 "$WORK/log.txt" 2>/dev/null; exit 1; }
rm -rf "$WORK"; mkdir -p "$WORK"
if [ ! -x "$B/engine/runtime/rensga_desktop" ] || [ ! -x "$B/tools/cooker/rensga-cooker" ]; then
  (cd "$SRC" && cmake --preset linux-host -B "$B" -DRENSGA_PROJECT_DIR="$SAMPLE" -DRENSGA_BUILD_EDITOR=OFF > "$WORK/cfg.txt" 2>&1 \
     && cmake --build "$B" -j"$RENSGA_JOBS" --target rensga_desktop rensga-cooker > "$WORK/build.txt" 2>&1) || { tail -20 "$WORK/cfg.txt" "$WORK/build.txt"; fail "sample build"; }
fi
mkdir -p "$WORK/proj"
cp -r "$SAMPLE/Assets" "$SAMPLE/ProjectSettings" "$WORK/proj/"
[ -d "$SAMPLE/Streaming" ] && cp -r "$SAMPLE/Streaming" "$WORK/proj/"
"$B/tools/cooker/rensga-cooker" cook --project "$WORK/proj" --target ps2 --force > "$WORK/cook.txt" 2>&1 || { cat "$WORK/cook.txt"; fail "cook"; }
env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy xvfb-run -a -s "-screen 0 1280x720x24" \
  "$B/engine/runtime/rensga_desktop" --pack "$WORK/proj/Library/Cooked/ps2/game.rpak" --scene InputTest --frames 90 --inject-demo \
  --screenshot "$WORK/input_test.png" > "$WORK/log.txt" 2>&1 || fail "rensga_desktop exit $?"
grep -qE 'rensga: alloc-check frames=[1-9][0-9]* allocs=0' "$WORK/log.txt" || fail "allocations while reading input"
[ -s "$WORK/input_test.png" ] || fail "no screenshot"
echo "exercise_input_desktop: ok ($WORK/input_test.png)"
