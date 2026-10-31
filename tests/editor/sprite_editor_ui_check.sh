#!/bin/bash
# F4.08: Sprite Editor and the SpriteRenderer sprite picker in the real editor under xvfb (screenshots land in the work dir).
# On a copy of tests/fixtures/paint2d: open player.png, slice a grid, add and edit a manual rectangle, pivots and names,
# undo, Apply (writes the .meta), Revert, then a SpriteRenderer picks a slice from the Inspector.
# Exit 77 = skipped (no xvfb-run / python3 / editor binary).
# usage: sprite_editor_ui_check.sh <rensga-editor> <tests/fixtures/paint2d> <work dir>
EDITOR_BIN="$1"; FIXTURE="$2"; WORK="$3"
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
command -v python3 >/dev/null 2>&1 || { echo "skip: no python3"; exit 77; }
[ -x "$EDITOR_BIN" ] || { echo "skip: no editor binary"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK/home"
cp -r "$FIXTURE" "$WORK/proj" && rm -rf "$WORK/proj/Library"
export RENSGA_HOME="$WORK/home" RENSGA_JOBS=${RENSGA_JOBS:-2}
LOG="$WORK/log.txt"
fail() { echo "FAIL: $1"; tail -60 "$LOG"; exit 1; }
EV=(openscene:Level
    spriteeditor:Assets/Textures/tiles.png spritedrag:2,2,10,12 spritedrag:5,5,9,9 spritedrag:14,16,20,22 spritedrag:6,6,10,12 spritedump
    slicegrid:16,16 spritedump
    spriteeditor:Assets/Textures/player.png slicegrid:16,16 "spritepivot:0|Bottom" "spritepivot:1|8,15" "spritename:1|player_walk"
    "spriterect:2,2,6,6,eye" spriteselect:0 "shot:$WORK/sprite_editor.png"
    spriteundo spriteundo spritedump spriteredo spriteredo spritedump
    sliceapply
    "spriterect:0,0,40,40,toobig" sliceapply
    slicerevert spritedump
    new:Hero addcomp:SpriteRenderer "setsprite:Hero|player_walk|Assets/Textures/player.png" select:Hero spritepick "shot:$WORK/sprite_picker.png"
    quit)
args=(); for e in "${EV[@]}"; do args+=(--ui-event "$e"); done
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR_BIN" --project "$WORK/proj" --no-splash --config-dir "$WORK/home/.config/rensga" --no-auto-rebuild --quit-after 600 "${args[@]}" > "$LOG" 2>&1 \
  || fail "editor exit $?"
grep -q "^sprite: editor Assets/Textures/tiles.png 64x32 slices=0 mode=manual" "$LOG" || fail "open tiles.png"
grep -q "^sprite: slice 0 sprite_0 6,6 14x16 pivot 4,6" "$LOG" || fail "mouse: draw a rectangle, move it, resize it by its corner, drag its pivot"
grep -q "^sprite: slicegrid cell=16x16 .* cols=4 rows=2 -> 8 slices" "$LOG" || fail "grid slice of the tileset"
grep -q "^sprite: slicegrid cell=16x16 .* cols=2 rows=1 -> 2 slices" "$LOG" || fail "grid slice of player.png"
grep -q "^sprite: spritepivot 0 -> 8,16" "$LOG" && grep -q "^sprite: spritepivot 1 -> 8,15" "$LOG" || fail "pivots"
grep -q "^sprite: spriterect index=2 eye" "$LOG" || fail "manual rect"
grep -q "^sprite: applied 3 slices to Assets/Textures/player.png.meta" "$LOG" || fail "apply"
grep -q "^sprite: apply failed" "$LOG" || fail "an out-of-bounds rect must not be applied"
grep -q "^sprite: reverted to 3 saved slices" "$LOG" || fail "revert"
grep -q "^sprite: setsprite Hero slice=player_walk src=16,0,16,16 pivot=8,15" "$LOG" || fail "the picker did not set the named slice"
python3 - "$LOG" <<'PY' || fail "undo/redo of the slice edits"
import re, sys
dumps = [l.strip() for l in open(sys.argv[1]) if l.startswith("sprite: dump")]
counts = [int(re.search(r"slices=(\d+)", d).group(1)) for d in dumps]
# 0: one rectangle drawn with the mouse; 1: tileset (8 slices); 2: after two undos (the rectangle, then the rename): 2 slices; 3: after two redos: 3; 4: after Revert: 3 saved
assert counts == [1, 8, 2, 3, 3], dumps
PY
python3 - "$WORK/proj/Assets/Textures/player.png.meta" <<'PY' || fail ".meta of player.png"
import json, sys
m = json.load(open(sys.argv[1]))
sp = m["sprites"]
assert [r["name"] for r in sp["rects"]] == ["player_0", "player_walk", "eye"], [r["name"] for r in sp["rects"]]
assert sp["rects"][0]["pivotX"] == 8 and sp["rects"][0]["pivotY"] == 16 and sp["rects"][1]["pivotY"] == 15
PY
[ -s "$WORK/sprite_editor.png" ] && [ -s "$WORK/sprite_picker.png" ] || fail "no screenshots"
echo "sprite_editor_ui_check: ok"
