#!/bin/sh
# F6.02 exercise in the real editor: walk through the baked level in the console preview. A temp copy of samples/level_walk, scene
# LevelBot (bakeLighting on every mesh, the LevelWalker script walks the route by itself): Play in editor, the player walks from the
# first hall through the doorway (position, collision against the walls, grounded), Game view screenshots while walking, Stop restores the
# scene file byte for byte. Exit 77 = skipped. usage: toolset3d_play_ui.sh <rensga-editor> <level_walk dir> <work dir>
EDITOR=$1; LEVEL=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
[ -d "$LEVEL" ] || { echo "skip: no project $LEVEL"; exit 77; }
export RENSGA_JOBS=${RENSGA_JOBS:-2}
rm -rf "$WORK"; mkdir -p "$WORK/cfg" "$WORK/proj"
cp -r "$LEVEL/Assets" "$LEVEL/ProjectSettings" "$WORK/proj/"
BEFORE=$(sha256sum "$WORK/proj/Assets/Scenes/LevelBot.scene" | cut -d' ' -f1)
set -e
env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR" --config-dir "$WORK/cfg" --project "$WORK/proj" --no-splash --no-auto-rebuild --focus Game \
  --screenshot "$WORK/stopped.png" --screenshot-frames 6000 \
  --ui-event waitbuild --ui-event openscene:LevelBot --ui-event showgame --ui-event playedit --ui-event playwait:30 \
  --ui-event playdump:Player --ui-event shot:"$WORK/walk_start.png" --ui-event playwait:200 --ui-event playdump:Player --ui-event shot:"$WORK/walk_mid.png" \
  --ui-event playwait:420 --ui-event playdump:Player --ui-event shot:"$WORK/walk_far.png" --ui-event stopedit \
  > "$WORK/log.txt" 2>&1 || { cat "$WORK/log.txt"; exit 1; }
fail() { cat "$WORK/log.txt"; echo "toolset3d_play_ui: $1"; exit 1; }
AFTER=$(sha256sum "$WORK/proj/Assets/Scenes/LevelBot.scene" | cut -d' ' -f1)
[ "$BEFORE" = "$AFTER" ] || fail "the scene file changed during play"
grep -q "^play: started" "$WORK/log.txt" || fail "play did not start"
grep -q "^play-check: .*identical=1 dirty=0" "$WORK/log.txt" || fail "stop did not restore the scene"
grep "^playdump: Player" "$WORK/log.txt" > "$WORK/dumps.txt"
[ "$(wc -l < "$WORK/dumps.txt")" -eq 3 ] || fail "expected 3 playdump lines"
python3 - "$WORK/dumps.txt" <<'PY' || fail "the player did not walk the route"
import re, sys
pts = []
for line in open(sys.argv[1]):
    m = re.search(r"pos=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)", line)
    pts.append(tuple(float(v) for v in m.groups()))
x0, y0, z0 = pts[0]; x1, y1, z1 = pts[1]; x2, y2, z2 = pts[2]
assert abs(x0) < 3.0 and z0 > 6.0, pts            # the first hall, on its way from the start (0, 8) to the doorway
assert (x1 - x0) ** 2 + (z1 - z0) ** 2 > 4.0, pts  # it moved
assert (x2 - x1) ** 2 + (z2 - z1) ** 2 > 4.0, pts  # and kept moving
assert z2 < z0 - 4.0, pts                           # towards the back of the level, through a doorway
for y in (y0, y1, y2): assert 0.7 < y < 1.2, pts    # on the floor (controller centre ~ 0.9), never fell through or climbed a wall
assert all(abs(p[0]) < 10.0 and abs(p[2]) < 10.0 for p in pts), pts  # inside the walls
PY
for f in walk_start walk_mid walk_far stopped; do [ -s "$WORK/$f.png" ] || fail "no $f.png"; done
echo "toolset3d_play_ui: ok"
