#!/bin/bash
# F4.08: Tile Palette and tile painting in the real editor under xvfb (screenshots land in the work dir). On a copy of
# tests/fixtures/paint2d: a new tilemap, brushes (paint, rect, fill, rule, erase), tile flags, the shared undo history
# (a tile stroke, then a scene edit: undo takes the scene edit first, then the stroke), save, mouse painting.
# Exit 77 = skipped (no xvfb-run / python3 / editor binary).
# usage: tile_palette_ui_check.sh <rensga-editor> <tests/fixtures/paint2d> <work dir>
EDITOR_BIN="$1"; FIXTURE="$2"; WORK="$3"
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
command -v python3 >/dev/null 2>&1 || { echo "skip: no python3"; exit 77; }
[ -x "$EDITOR_BIN" ] || { echo "skip: no editor binary"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK/home"
cp -r "$FIXTURE" "$WORK/proj" && rm -rf "$WORK/proj/Library"
export RENSGA_HOME="$WORK/home" RENSGA_JOBS=${RENSGA_JOBS:-2}
LOG="$WORK/log.txt"
fail() { echo "FAIL: $1"; tail -60 "$LOG"; exit 1; }
EV=(openscene:Level mode:2d new:Ground addcomp:Tilemap "newtilemap:Level|Assets/Textures/tiles.png|16|24x12"
    tileflag:0=solid tileflag:2=oneway tileflag:4=slope45L
    brush:rect tile:1 paintrect:0,9,23,11 tile:0 paintrect:0,8,23,8
    undoname
    "setpos:Camera2D|10,20,0" undoname
    undo undoname dumptiles:0,8,5,9
    undo undoname dumptiles:0,8,5,9
    redo redo dumptiles:0,8,5,9
    brush:rule "ruletile:Assets/Tiles/ground.ruletile.json" "painttile:10,7;11,7;12,7" dumptiles:8,6,14,9
    brush:erase "painttile:11,7" dumptiles:8,6,14,9
    brush:fill tile:2 filltile:3,2 dumptiles:0,0,3,3
    select:Ground framesel brush:paint tile:3 tiledrag:2,4,8,4
    "shot:$WORK/palette.png" savetiles
    tile:4 "shot:$WORK/palette_slope.png"
    closetools select:Ground "shot:$WORK/inspector_tilemap.png"
    quit)
args=(); for e in "${EV[@]}"; do args+=(--ui-event "$e"); done
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR_BIN" --project "$WORK/proj" --no-splash --config-dir "$WORK/home/.config/rensga" --no-auto-rebuild --quit-after 600 "${args[@]}" > "$LOG" 2>&1 \
  || fail "editor exit $?"
grep -q "^tile: newtilemap .*24x12 assigned=1" "$LOG" || fail "new tilemap"
grep -q "^tile: paintrect .*writes=72 " "$LOG" || fail "rect brush"
# undo order: the scene edit (setpos) is on top of the last tile stroke
python3 - "$LOG" <<'PY' || fail "undo order or tile contents"
import re, sys
lines = [l.rstrip("\n") for l in open(sys.argv[1])]
tops = [re.match(r"tile: undo-top='([^']*)' depth=(\d+) redo-top='([^']*)'", l) for l in lines if l.startswith("tile: undo-top")]
tops = [m.groups() for m in tops]
names = [t[0] for t in tops]
assert names[0] == "Paint Tile Rectangle", names           # right after painting
assert names[1] not in ("Paint Tile Rectangle", ""), names  # the scene edit sits on top
assert names[2] == "Paint Tile Rectangle" and tops[2][2] == names[1], tops   # undo 1 took the scene edit, not the tiles
assert names[3] == "Paint Tile Rectangle" and tops[3][2] == "Paint Tile Rectangle", tops   # undo 2 took the last stroke
dumps = [i for i, l in enumerate(lines) if l.startswith("tile: dump")]
def row(i, y):
    for l in lines[i:i + 20]:
        m = re.match(r"tile: row %d: (.*)" % y, l)
        if m: return m.group(1).split()
def filled(i): return int(re.search(r"filled=(\d+)", lines[i]).group(1))
# dumps: 0 = after undo 1, 1 = after undo 2, 2 = after redo redo, 3 = rule, 4 = erase, 5 = fill
assert filled(dumps[0]) == 96 and row(dumps[0], 8)[0] == "0", lines[dumps[0]]   # tiles still there after undoing the scene edit
assert filled(dumps[1]) == 72 and row(dumps[1], 8)[0] == "." and row(dumps[1], 9)[0] == "1", lines[dumps[1]]   # second undo took the row 8 stroke
assert filled(dumps[2]) == 96, lines[dumps[2]]
# rule tile: three cells placed in a row on y=7 above the grass row: the ones with nothing above become grass (0), the grass row below gets dirt
r7 = row(dumps[3], 7)
assert r7[2:5] == ["0", "0", "0"], r7                       # columns 10..12 (dump starts at x=8)
assert row(dumps[3], 8)[2:5] == ["1", "1", "1"], row(dumps[3], 8)
r7e = row(dumps[4], 7)
assert r7e[3] == "." and r7e[2] == "0" and r7e[4] == "0", r7e
assert row(dumps[5], 0) == ["2"] * 4 and row(dumps[5], 3) == ["2"] * 4, row(dumps[5], 0)
PY
grep -q "^tile: saved Assets/Textures/Level.tilemap.json" "$LOG" || fail "savetiles"
[ -s "$WORK/palette.png" ] && [ -s "$WORK/palette_slope.png" ] && [ -s "$WORK/inspector_tilemap.png" ] || fail "no screenshots"
python3 - "$WORK/proj/Assets/Textures/Level.tilemap.json" <<'PY' || fail "saved tilemap"
import json, sys
tm = json.load(open(sys.argv[1]))
assert tm["width"] == 24 and tm["height"] == 12
assert all(tm["tiles"][4 * 24 + x] == 3 for x in range(2, 9)), tm["tiles"][4 * 24:5 * 24]   # the mouse stroke 2,4 -> 8,4
assert tm["tiles"][4 * 24 + 1] == 2 and tm["tiles"][4 * 24 + 9] == 2   # the fill around it
assert tm["solid"] == [0] and tm["oneWay"] == [2] and tm["slopes"] == {"4": "45L"}, tm
PY
echo "tile_palette_ui_check: ok"
