#!/bin/sh
# F5.06 / F5.07 / F5.08 in the real editor under xvfb, on a temp copy of samples/psp3d_room:
#   - Lights12: the Inspector's validation list names the objects reached by more than the target's light limit; clicking selects one
#   - Game view: 16-bit output with ordered dither + device frame + safe area + budget bars (screenshot), preview verbs print the options
#   - asset inspector, build target tab: the room texture (downscale / power-of-two notes, VRAM/RAM cost) and the blocky glTF (vertex precision, cost)
#   - a copy of the project whose ConsoleTarget lacks the skinning/fog features and carries a "fieldRules" entry: the Inspector greys those fields with the reason
# Exit 77 = skipped (no xvfb-run / editor).
# usage: preview_ui_check.sh <rensga-editor> <samples/psp3d_room dir> <work dir>
EDITOR=$1; ROOM=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
[ -d "$ROOM" ] || { echo "skip: no project $ROOM"; exit 77; }
export RENSGA_JOBS=${RENSGA_JOBS:-2}
rm -rf "$WORK"; mkdir -p "$WORK/cfg" "$WORK/proj" "$WORK/gated"
cp -r "$ROOM/Assets" "$ROOM/ProjectSettings" "$WORK/proj/"
cp -r "$ROOM/Assets" "$ROOM/ProjectSettings" "$WORK/gated/"
run() {  # run <name> <project> <shot> <events...>
  name=$1; proj=$2; shot=$3; shift 3
  args=""
  for e in "$@"; do args="$args --ui-event $e"; done
  # shellcheck disable=SC2086
  env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" xvfb-run -a -s "-screen 0 1600x900x24" \
    "$EDITOR" --config-dir "$WORK/cfg" --project "$proj" --no-splash --screenshot "$shot" --screenshot-frames 40 $args \
    > "$WORK/$name.txt" 2>&1 || { cat "$WORK/$name.txt"; echo "$name: editor failed"; exit 1; }
}
set -e
# 1. validation list on the 12-light scene + preview options
run validation "$WORK/proj" "$WORK/validation.png" openscene:Lights12 validate previewdump preview:dither=1,frame=1,safe=1,color=16 previewdump
run selecting "$WORK/proj" "$WORK/selecting.png" openscene:Lights12 validatesel:0 dumpsel
grep -q "^validate: [0-9]* finding" "$WORK/validation.txt" || { cat "$WORK/validation.txt"; echo "no validate line"; exit 1; }
N=$(grep -c "^validate-item: .* warning lights " "$WORK/validation.txt" || true)
[ "${N:-0}" -ge 3 ] || { cat "$WORK/validation.txt"; echo "expected >= 3 light findings, got $N"; exit 1; }
grep -q "^validate: selected " "$WORK/selecting.txt" || { cat "$WORK/selecting.txt"; echo "finding did not select an object"; exit 1; }
grep -q "colorBits=16 dither=1 maxLights=4" "$WORK/validation.txt" || { cat "$WORK/validation.txt"; echo "preview rules not data driven"; exit 1; }
grep -q "quantize=1 dither=1 frame=1 safe=1" "$WORK/validation.txt" || { cat "$WORK/validation.txt"; echo "preview verbs"; exit 1; }
# 2. Game view in edit mode with the device frame, dither and bars
run gameview "$WORK/proj" "$WORK/gameview.png" openscene:Lights12 showgame preview:dither=1,frame=1,safe=1,color=16,bars=1
run gameplay "$WORK/proj" "$WORK/gameplay.png" openscene:Room preview:dither=1,frame=1,safe=1,color=16,bars=1 showgame playedit playwait:30
grep -q "^play: started" "$WORK/gameplay.txt" || { cat "$WORK/gameplay.txt"; echo "Play in editor did not start"; exit 1; }
run gameplay_off "$WORK/proj" "$WORK/gameplay_off.png" openscene:Room preview:dither=0,frame=0,safe=0,color=32,textures=0,lights=0 showgame playedit playwait:30
# 3. import tab of the room texture and of the glTF
run importtex "$WORK/proj" "$WORK/import_texture.png" asset:Assets/Textures/room.png assettab:psp
run importmodel "$WORK/proj" "$WORK/import_model.png" asset:Assets/Models/blocky_walk.gltf assettab:psp
# 4. field gating
python3 - "$WORK/gated/ProjectSettings/ConsoleTarget.json" <<'PY'
import json, sys
p = sys.argv[1]
j = json.load(open(p))
j["features"] = ["lighting", "textureSwizzle"]
j["fieldRules"] = [{"component": "Light", "field": "renderMode", "reason": "This target ignores the render mode: every light is sorted by strength."}]
json.dump(j, open(p, "w"), indent=1)
PY
run gating "$WORK/gated" "$WORK/gating_light.png" openscene:Lights12 select:Light0
run gating2 "$WORK/gated" "$WORK/gating_skin.png" openscene:Room select:Character
run gating3 "$WORK/gated" "$WORK/gating_fog.png" openscene:Room select:RenderSettings
for f in validation gameview gameplay gameplay_off import_texture import_model gating_light gating_skin gating_fog; do [ -s "$WORK/$f.png" ] || { echo "no screenshot $f.png"; exit 1; }; done
echo "preview_ui_check: ok ($N light findings; screenshots in $WORK)"
