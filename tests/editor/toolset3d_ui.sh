#!/bin/sh
# F6.02 (Editor 8) in the real editor under xvfb, on a temp copy of samples/level_walk (a glTF level):
#   1. the Scene view draws the lit scene (litdump: objects, lights in the frame, the target's 4 lights per object) = screenshot "lit"
#   2. GameObject > Light creates the F5 Light (directional and point), 3D Object > camera / audio source work
#   3. the Material Editor edits walls.mat.json (colour, blend mode, unlit refused or accepted by the target) and the file changes
#   4. Lighting > Bake all flags every mesh, bakes on a worker (bakewait), the Scene view recooks = screenshot "baked"; clear again
# Exit 77 = skipped (no xvfb-run / editor). usage: toolset3d_ui.sh <rensga-editor> <level_walk dir> <work dir>
EDITOR=$1; LEVEL=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
[ -d "$LEVEL" ] || { echo "skip: no project $LEVEL"; exit 77; }
export RENSGA_JOBS=${RENSGA_JOBS:-2}
rm -rf "$WORK"; mkdir -p "$WORK/cfg" "$WORK/proj"
cp -r "$LEVEL/Assets" "$LEVEL/ProjectSettings" "$WORK/proj/"
set -e
EYE="-9,9,13|0,0.6,-1|60"   # the whole level from the south-west, above the walls
env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR" --config-dir "$WORK/cfg" --project "$WORK/proj" --no-splash --screenshot "$WORK/final.png" --screenshot-frames 3000 \
  --ui-event waitbuild --ui-event openscene:LevelRaw --ui-event scenecam:$EYE --ui-event editworld --ui-event litdump \
  --ui-event litdump --ui-event litdump --ui-event litdump --ui-event litdump --ui-event litdump --ui-event shot:"$WORK/lit.png" --ui-event litdump \
  --ui-event select:Lamp1 --ui-event litdump --ui-event litdump --ui-event shot:"$WORK/lamp.png" --ui-event select:Camera --ui-event litdump --ui-event litdump --ui-event litdump \
  --ui-event shot:"$WORK/camera.png" --ui-event select:Walls --ui-event litdump --ui-event shot:"$WORK/walls.png" \
  --ui-event create:light --ui-event dumpsel --ui-event create:pointlight --ui-event dumpsel --ui-event create:audio --ui-event dumpsel \
  --ui-event create:camera --ui-event dumpsel --ui-event undo --ui-event undo --ui-event undo --ui-event undo \
  --ui-event matedit:Assets/Materials/walls.mat.json --ui-event matset:color=255,200,200 --ui-event matset:blend=alpha --ui-event matset:alpha=200 \
  --ui-event matset:unlit=1 --ui-event matdump --ui-event shot:"$WORK/material.png" --ui-event matundo --ui-event matundo --ui-event matdump --ui-event matset:blend=opaque \
  --ui-event matset:color=255,255,255 --ui-event matset:unlit=0 --ui-event matdump --ui-event matclose \
  --ui-event lighting --ui-event bakedump --ui-event bakeall --ui-event bakewait --ui-event bakedump --ui-event shot:"$WORK/lighting.png" --ui-event editworld \
  --ui-event litdump --ui-event litdump --ui-event litdump --ui-event litdump --ui-event litdump --ui-event litdump \
  --ui-event shot:"$WORK/baked.png" --ui-event bakeclear --ui-event bakedump --ui-event litdump \
  > "$WORK/log.txt" 2>&1 || { cat "$WORK/log.txt"; exit 1; }
fail() { cat "$WORK/log.txt"; echo "toolset3d_ui: $1"; exit 1; }
# 1. lit view: the 3 glTF meshes of the level and 8 lights (7 lamps + the moon) in the frame; at most 4 lights per object on this target
grep -E "^lit: frames=[1-9][0-9]* draws=[1-9][0-9]* objects=3 lights=8 maxLights=4 " "$WORK/log.txt" >/dev/null || fail "no lit frame with 3 objects and 8 lights"
# 2. creation menu paths: GameObject > Light creates the F5 Light (directional, then point), plus the audio source and the camera
grep -q "selection=\[Directional Light\]" "$WORK/log.txt" || fail "no Directional Light created"
grep -q "selection=\[Point Light\]" "$WORK/log.txt" || fail "no Point Light created"
grep -q "selection=\[Audio Source\]" "$WORK/log.txt" || fail "no Audio Source created"
# 3. material editor: opened, edits reach the file, undo works, the edits are visible in the printed state
grep -q "^material: open Assets/Materials/walls.mat.json" "$WORK/log.txt" || fail "walls.mat.json did not open"
grep -q "^material: Assets/Materials/walls.mat.json .*color=255,200,200,200 unlit=1 .*blend=alpha" "$WORK/log.txt" || fail "colour / alpha / unlit edits not applied"
grep -q "^material: Assets/Materials/walls.mat.json .*blend=opaque .*undo=" "$WORK/log.txt" || fail "no material state after undo"
python3 - "$WORK/proj/Assets/Materials/walls.mat.json" <<'PY' || fail "walls.mat.json on disk is not the restored material"
import json, sys
m = json.load(open(sys.argv[1]))
assert m["color"] == [255, 255, 255, 255], m
assert m["unlit"] is False, m
PY
# 4. bake: flagged, ran on a worker, produced meshes; cleared again
grep -E "^bake: flagged 3 meshes, started" "$WORK/log.txt" >/dev/null || fail "bakeall did not flag the 3 meshes"
grep -E "^bake: running=0 meshes=3 flagged=3 lights=8 meshesBaked=[3-9][0-9]* vertices=[1-9][0-9]* rays=[0-9]+ " "$WORK/log.txt" >/dev/null || fail "no finished bake with 3 meshes"
grep -E "^bake: running=0 meshes=3 flagged=0 " "$WORK/log.txt" >/dev/null || fail "bakeclear did not unflag"
for f in lit lamp camera walls material lighting baked final; do [ -s "$WORK/$f.png" ] || fail "no $f.png"; done
echo "toolset3d_ui: ok"
