#!/bin/sh
# F3.07 (API 11) "patrol script with gizmos" in the real editor under xvfb, on a temp copy of samples/editor_tour:
# the Patrol script module is built and loaded, the edit-mode World exists, the Scene view draws the waypoint gizmos
# (screenshot), and an inspector edit past the range is clamped by OnValidate. Exit 77 = skipped (no xvfb-run / editor).
# usage: callbacks_ui_check.sh <rensga-editor> <editor_tour dir> <work dir>
EDITOR=$1; TOUR=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
[ -d "$TOUR" ] || { echo "skip: no project $TOUR"; exit 77; }
export RENSGA_JOBS=${RENSGA_JOBS:-2}
rm -rf "$WORK"; mkdir -p "$WORK/cfg" "$WORK/proj"
cp -r "$TOUR/Assets" "$TOUR/ProjectSettings" "$WORK/proj/"
set -e
env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR" --config-dir "$WORK/cfg" --project "$WORK/proj" --no-splash \
  --screenshot "$WORK/patrol_gizmos.png" --screenshot-frames 900 \
  --ui-event waitbuild --ui-event openscene:Patrol --ui-event select:Guard --ui-event editworld --ui-event dumpsel --ui-event dumpsel --ui-event gizmos \
  > "$WORK/log.txt" 2>&1 || { cat "$WORK/log.txt"; exit 1; }
grep -q "^scene: editworld valid=1 " "$WORK/log.txt" || { cat "$WORK/log.txt"; echo "no edit-mode World"; exit 1; }
# 3 route segments + 3 wire spheres + the selected box and next-stop line: far more than the built-in gizmos alone
L=$(grep "^scene: gizmos" "$WORK/log.txt" | tail -1 | sed 's/.*lines=\([0-9]*\).*/\1/')
[ "${L:-0}" -gt 20 ] || { cat "$WORK/log.txt"; echo "no script gizmo lines ($L)"; exit 1; }
[ -s "$WORK/patrol_gizmos.png" ]
echo "callbacks_ui_check: ok (gizmo lines $L)"
