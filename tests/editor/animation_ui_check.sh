#!/bin/sh
# F6.03 (Editor 9): the Animation window under xvfb on samples/animator_player. Opens the Run clip, scrubs it (preview on the scene),
# records edits made through the Inspector and the Scene view into keys, undoes a key, saves the clip, and checks that the scene file
# was never touched and that the saved clip still cooks. Screenshots: dopesheet, curve editor.
# Exit 77 = skipped (no xvfb-run / editor binary).
# usage: animation_ui_check.sh <rensga-editor> <project dir> <work dir>
EDITOR=$1; SAMPLE=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
[ -d "$SAMPLE/Assets" ] || { echo "skip: no project $SAMPLE"; exit 77; }
set -e
rm -rf "$WORK"; mkdir -p "$WORK/proj" "$WORK/cfg"
cp -r "$SAMPLE/Assets" "$SAMPLE/ProjectSettings" "$WORK/proj/"
SCENE="$WORK/proj/Assets/Scenes/Main.scene"
BEFORE=$(sha256sum "$SCENE" | cut -d' ' -f1)
CLIP=Assets/Animation/Run.clip.json
run() { out=$1; log=$2; shift 2; A=""; for e in "$@"; do A="$A --ui-event $e"; done
  env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" xvfb-run -a -s "-screen 0 1600x900x24" \
    "$EDITOR" --config-dir "$WORK/cfg" --project "$WORK/proj" --no-splash --no-auto-rebuild --focus Scene \
    --screenshot "$out" --screenshot-frames 150 $A > "$log" 2>&1 || { cat "$log"; exit 1; }; }
need() { grep -qE "$1" "$2" || { cat "$2"; echo "missing: $1 ($3)"; exit 1; }; }

# 1. open, scrub: the Run clip steps srcX through 0, 32, 64, 96 and moves the sprite row (srcY 32)
run "$WORK/dopesheet.png" "$WORK/log1.txt" select:Player animwindow "animclip:$CLIP" animroot:Player \
  animscrub:2 "animvalue:Player|SpriteRenderer.srcX" "animvalue:Player|SpriteRenderer.srcY" \
  animscrub:3 "animvalue:Player|SpriteRenderer.srcX" animdump "shot:$WORK/dopesheet_mid.png"
need "^anim: clip $CLIP fps=12 length=4 curves=2 events=0 preview=on" "$WORK/log1.txt" "clip opened with preview"
need "^anim: value Player SpriteRenderer.srcX = 64" "$WORK/log1.txt" "tick 2 value"
need "^anim: value Player SpriteRenderer.srcY = 32" "$WORK/log1.txt" "srcY step"
need "^anim: value Player SpriteRenderer.srcX = 96" "$WORK/log1.txt" "tick 3 value"

# 2. record: an Inspector edit and a Scene view move become keys at the playhead; the document itself stays untouched
run "$WORK/curves.png" "$WORK/log2.txt" select:Player animwindow "animclip:$CLIP" animroot:Player animcurves:on \
  animrecord:on animscrub:2 "setfield:SpriteRenderer.color=[10,20,30,255]" "setpos:Player|99,5,0" \
  animscrub:0 "animvalue:Player|SpriteRenderer.color.r" animscrub:2 "animvalue:Player|SpriteRenderer.color.r" "animvalue:Player|Transform.localPosition.x" \
  animdump animrecord:off "shot:$WORK/curves_shot.png" animsave animpreview:off "savecopy:$WORK/scene_after.scene"
need "keysRecorded=[1-9]" "$WORK/log2.txt" "record made keys"
need "^anim: value Player SpriteRenderer.color.r = 10" "$WORK/log2.txt" "recorded colour at tick 2"
need "^anim: value Player Transform.localPosition.x = 99" "$WORK/log2.txt" "recorded position at tick 2"
need "curve [0-9]+ SpriteRenderer.color.r interp=linear keys=1 2:10" "$WORK/log2.txt" "colour.r key at tick 2"
need "^anim: saved $CLIP" "$WORK/log2.txt" "clip saved"
python3 -I - "$WORK/proj/$CLIP" <<'PY'
import json, sys
c = json.load(open(sys.argv[1]))
props = {x["property"]: x for x in c["curves"]}
assert "SpriteRenderer.color.r" in props and "Transform.localPosition.x" in props, sorted(props)
assert props["Transform.localPosition.x"]["keys"] == [{"t": 2, "v": 99.0}], props["Transform.localPosition.x"]
assert any(k["t"] == 2 and k["v"] == 10 for k in props["SpriteRenderer.color.r"]["keys"])
PY
cmp -s "$WORK/scene_after.scene" "$SCENE" || { echo "the preview changed the scene document"; diff "$WORK/scene_after.scene" "$SCENE" | head; exit 1; }
[ "$BEFORE" = "$(sha256sum "$SCENE" | cut -d' ' -f1)" ] || { echo "scene file changed"; exit 1; }

# 3. events and undo: add an event, undo it, add it again; the saved clip keeps it
run "$WORK/events.png" "$WORK/log3.txt" select:Player animwindow "animclip:$CLIP" "animevent:1|Footstep|0|0.5" animdump animundo animdump "animevent:2|Footstep|0|0.7" animsave
need "events=1" "$WORK/log3.txt" "event added"
python3 -I - "$WORK/proj/$CLIP" <<'PY'
import json, sys
c = json.load(open(sys.argv[1]))
assert [e["t"] for e in c["events"]] == [2] and c["events"][0]["function"] == "Footstep", c["events"]
PY
[ -s "$WORK/dopesheet.png" ] && [ -s "$WORK/curves.png" ] && [ -s "$WORK/events.png" ]
echo "animation_ui_check: ok"
