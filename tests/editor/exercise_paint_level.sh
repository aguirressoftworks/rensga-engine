#!/bin/bash
# Editor 7 exercise "paint a level" (F4.08, label f4_exercises): on a fresh copy of tests/fixtures/paint2d, make a scene with a
# Tilemap (a new tilemap asset from the tileset), paint ground and a platform with the palette verbs and a real mouse stroke,
# set the tile flags, add TilemapCollider2D, make a player (SpriteRenderer from slices cut in the Sprite Editor, BoxCollider2D,
# Rigidbody2D, a script), save, Play in editor, and check that the player rests on the painted ground. Stop restores the scene.
# usage: exercise_paint_level.sh <rensga-editor> <tests/fixtures/paint2d> <work dir>
. "$(dirname "$0")/exercise_lib.sh"
ex_init "$1" "$2" "$3"
PROJ="$WORK/proj"
ex_run "$WORK/log.txt" waitbuild openscene:Level mode:2d \
  new:Ground addcomp:Tilemap addcomp:TilemapCollider2D "newtilemap:Level|Assets/Textures/tiles.png|16|40x15" \
  tileflag:0=solid tileflag:1=solid tileflag:2=oneway tileflag:3=solid tileflag:4=slope45L tileflag:5=slope45R \
  brush:rect tile:1 paintrect:0,13,39,14 tile:0 paintrect:0,12,39,12 \
  brush:paint tile:2 "painttile:10,8;11,8;12,8;13,8" \
  select:Ground framesel brush:paint tile:3 tiledrag:30,10,34,10 \
  brush:fill tile:3 filltile:20,3 undo \
  dumptiles:0,6,39,14 "shot:$WORK/painted.png" \
  new:Player setpos:Player\|64,100,0 \
  spriteeditor:Assets/Textures/player.png slicegrid:16,16 "spritepivot:0|Bottom" "spritepivot:1|Bottom" sliceapply spritedump \
  addcomp:SpriteRenderer "setsprite:Player|player_0|Assets/Textures/player.png" \
  addcomp:BoxCollider2D setfield:BoxCollider2D.offsetX=-6 setfield:BoxCollider2D.offsetY=-16 setfield:BoxCollider2D.sizeX=12 setfield:BoxCollider2D.sizeY=16 \
  addcomp:Rigidbody2D addscript:PaintPlayer \
  save "savecopy:$WORK/before_play.scene" \
  closetools showgame playedit playwait:90 "playdump:Player" playkeys:Right playwait:200 "playdump:Player" "shot:$WORK/play.png" playkeys:none \
  stopedit "shot:$WORK/stopped.png" quit
[ $EX_RC -eq 0 ] || ex_fail "editor exit $EX_RC"

grep -q "^tile: newtilemap Assets/Textures/Level.tilemap.json .* assigned=1" "$LOG" || ex_fail "the new tilemap was not created and assigned to Ground"
grep -q "^tile: flag 2=oneway ok" "$LOG" && grep -q "^tile: flag 4=slope45L ok" "$LOG" || ex_fail "tile flags refused"
grep -q "^tile: paintrect .*writes=80 " "$LOG" || ex_fail "ground rectangle not painted (expected 80 cells)"
grep -q "^tile: painttile brush=paint cells=4 writes=4" "$LOG" || ex_fail "platform not painted"
grep -q "^tile: tiledrag 2 point" "$LOG" || ex_fail "tiledrag verb"
grep -q "^tile: filltile (20,3) writes=471 " "$LOG" || ex_fail "fill did not cover the empty area (600 cells - 129 painted)"
grep -q "^sprite: slicegrid cell=16x16 .* -> 2 slices" "$LOG" && grep -q "^sprite: applied 2 slices" "$LOG" || ex_fail "slices not cut and applied"
grep -q "^sprite: setsprite Player slice=player_0 src=0,0,16,16 pivot=8,16" "$LOG" || ex_fail "the sprite picker did not set the slice"

# the stroke by mouse and the undone fill: row 10 holds the dragged bricks (tile 3) at x 30..34, and the fill (600 cells of tile 3) is gone
DUMP=$(awk '/^tile: dump/{n++} n==1' "$LOG")
echo "$DUMP" | grep -q "^tile: row 10: .*" || ex_fail "no dump"
python3 - "$LOG" <<'PY' || ex_fail "painted tiles are not what the verbs and the mouse stroke should make"
import re, sys
rows = {}
for line in open(sys.argv[1]):
    m = re.match(r"tile: row (\d+): (.*)", line.strip())
    if m and int(m.group(1)) not in rows: rows[int(m.group(1))] = m.group(2).split()
assert rows[12][:40] == ["0"] * 40, rows[12]
assert rows[13] == ["1"] * 40 and rows[14] == ["1"] * 40
assert [rows[8][i] for i in range(10, 14)] == ["2"] * 4 and rows[8][9] == "." and rows[8][14] == "."
assert [rows[10][i] for i in range(30, 35)] == ["3"] * 5, rows[10]   # painted with the mouse
assert rows[10][29] == "." and rows[10][35] == ".", rows[10]
assert rows[6][0] == ".", "the undone fill left tiles behind"
PY

# on disk: the tilemap (saved with the scene), the slices in player.png.meta, the sprite fields in the scene
TM="$PROJ/Assets/Textures/Level.tilemap.json"
[ -f "$TM" ] || ex_fail "Level.tilemap.json was not written"
python3 - "$TM" "$PROJ/Assets/Textures/player.png.meta" "$PROJ/Assets/Scenes/Level.scene" <<'PY' || ex_fail "files on disk are wrong"
import json, sys
tm = json.load(open(sys.argv[1]))
assert tm["width"] == 40 and tm["height"] == 15 and tm["tileSize"] == 16
assert tm["tiles"][12 * 40 + 5] == 0 and tm["tiles"][13 * 40 + 5] == 1 and tm["tiles"][8 * 40 + 11] == 2
assert sorted(tm["solid"]) == [0, 1, 3] and tm["oneWay"] == [2], tm
assert tm["slopes"] == {"4": "45L", "5": "45R"}, tm.get("slopes")
meta = json.load(open(sys.argv[2]))
sp = meta["sprites"]
assert [r["name"] for r in sp["rects"]] == ["player_0", "player_1"] and sp["rects"][0]["pivotX"] == 8 and sp["rects"][0]["pivotY"] == 16, sp
scene = json.load(open(sys.argv[3]))
by = {o["name"]: o for o in scene["objects"]}
types = [c["type"] for c in by["Player"]["components"]]
assert types == ["SpriteRenderer", "BoxCollider2D", "Rigidbody2D", "PaintPlayer"], types
sr = by["Player"]["components"][0]["fields"]
assert sr["srcW"] == 16 and sr["srcH"] == 16 and sr["pivotY"] == 16 and sr["texture"], sr
gt = [c["type"] for c in by["Ground"]["components"]]
assert gt == ["Tilemap", "TilemapCollider2D"] and by["Ground"]["components"][0]["fields"]["map"], gt
PY

# Play: the player falls from y=100 and rests on the ground (top of row 12 = y 192), then walks right with the pad
grep -q "^play: started" "$LOG" || ex_fail "play did not start"
grep "^playdump: Player" "$LOG" > "$WORK/dumps.txt"
[ $(wc -l < "$WORK/dumps.txt") -eq 2 ] || ex_fail "expected two player dumps"
python3 - "$WORK/dumps.txt" <<'PY' || ex_fail "the player is not standing on the painted ground"
import re, sys
pos = [tuple(float(v) for v in re.search(r"pos=([-0-9.]+),([-0-9.]+),([-0-9.]+)", l).groups()) for l in open(sys.argv[1])]
print("player rest:", pos[0], "after walking:", pos[1])
assert abs(pos[0][1] - 192) <= 1.5, "y at rest %r, expected 192" % (pos[0],)
assert abs(pos[0][0] - 64) <= 1.5, "x at rest %r" % (pos[0],)
assert pos[1][0] > 64 + 40, "the pad did not move the player: %r" % (pos[1],)
assert abs(pos[1][1] - 192) <= 1.5, "the player left the ground while walking: %r" % (pos[1],)
PY
grep -q "^play-check: .*identical=1 dirty=0 dirty_before=0 " "$LOG" || ex_fail "Stop did not restore the scene"
UNDO=$(sed -n 's/^play-check: .* undo=\([0-9]*\) undo_before=\([0-9]*\) .*/\1 \2/p' "$LOG")
[ "${UNDO% *}" = "${UNDO#* }" ] || ex_fail "the undo history changed during play: $UNDO"
cmp -s "$WORK/before_play.scene" "$PROJ/Assets/Scenes/Level.scene" || ex_fail "the scene file changed during play"
for f in painted play stopped; do [ -s "$WORK/$f.png" ] || ex_fail "no screenshot $f.png"; done
echo "exercise_paint_level: ok"
