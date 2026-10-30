#!/bin/sh
# F3.01 / gate F3.G1 in the real editor under xvfb, on temp copies of samples/platformer2d (2D mode) and templates/psp (3D):
#  - exercise "save and reopen a scene": build a three-level nested scene, save, reopen from disk, save again: no diff;
#  - multi-select reparent / reorder / duplicate / copy-paste / delete with undo, as one step each;
#  - picking through the ID buffer (sprites, tilemaps, meshes, markers) and box selection;
#  - screenshots: 3D scene with gizmos, Room in 2D mode with collider and camera gizmos.
# Exit 77 = skipped (no xvfb-run).
# usage: scene_ui_check.sh <rensga-editor> <platformer2d dir> <psp template dir> <work dir>
EDITOR=$1; SAMPLE=$2; PSP=$3; WORK=$4
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
set -e
rm -rf "$WORK"; mkdir -p "$WORK/plat" "$WORK/psp" "$WORK/cfg"
for p in plat:"$SAMPLE" psp:"$PSP"; do
  d=${p%%:*}; s=${p#*:}
  cp -r "$s/Assets" "$s/ProjectSettings" "$WORK/$d/"
  rm -rf "$WORK/$d/Assets/Scripts"          # no scripts: no script module build
done
run() { proj=$1; out=$2; shift 2; A=""; n=0; for e in "$@"; do A="$A --ui-event $e"; n=$((n+1)); done
  env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" xvfb-run -a -s "-screen 0 1600x900x24" \
    "$EDITOR" --config-dir "$WORK/cfg" --project "$WORK/$proj" --no-splash --no-auto-rebuild \
    --screenshot "$out" --screenshot-frames $((n * 6 + 60)) $A; }
# n-th "scene: tree" block of a log, without selection marks
block() { awk -v n="$2" '/^scene: tree/{c++; next} /^scene: /{if(c==n)exit} c==n{print}' "$1" | sed 's/ \*$//'; }

# ---- 2D: nested scene, save, reopen, save; reparent + undo; picking ----
run plat "$WORK/room2d.png" openscene:Room mode:2d new:Nest1 new:Nest2\|Nest1 new:Nest3\|Nest2 \
  selectmany:Coin1,Coin2,Coin3 reparent:Coin1,Coin2,Coin3\|Nest3 dumptree dumpsel \
  undo dumptree redo dumptree \
  save savecopy:"$WORK/s1.scene" reload savecopy:"$WORK/never.scene" dumptree save savecopy:"$WORK/s2.scene" \
  selectmany:Player,Walker1 framesel pickobj:Player pickobj:Walker1 pickobj:Coin2 gizmos dumpsel > "$WORK/log2d.txt" 2>&1
cmp "$WORK/s1.scene" "$WORK/s2.scene"                                   # saved, reloaded, saved again: no diff
grep -q "savecopy ok" "$WORK/log2d.txt"
block "$WORK/log2d.txt" 1 | grep -q "^        Coin1$"                    # three levels deep under Nest1 > Nest2 > Nest3
block "$WORK/log2d.txt" 1 | grep -q "^      Nest3$"
block "$WORK/log2d.txt" 2 | grep -q "^  Coin1$"                           # undo: one step puts all three back at the root
block "$WORK/log2d.txt" 2 | grep -q "^  Coin3$"
block "$WORK/log2d.txt" 3 | grep -q "^        Coin3$"                    # redo
block "$WORK/log2d.txt" 3 > "$WORK/tree_before.txt"
block "$WORK/log2d.txt" 4 > "$WORK/tree_after.txt"                        # tree of the reopened file
diff "$WORK/tree_before.txt" "$WORK/tree_after.txt"
grep -q "pickobj Player -> Player" "$WORK/log2d.txt"
grep -q "pickobj Walker1 -> Walker1" "$WORK/log2d.txt"
grep -q "pickobj Coin2 -> Coin2" "$WORK/log2d.txt"
grep -q "gizmos lines=[1-9][0-9]* .* mode=2d" "$WORK/log2d.txt"
[ -s "$WORK/room2d.png" ]

# ---- 3D: multi-select edits, picking, box select ----
run psp "$WORK/scene3d.png" selectmany:Platform1,Platform2,Platform3 reparent:Platform1,Platform2,Platform3\|Ground dumptree \
  undo dumptree selectmany:Pickup1,Pickup2 dup dumptree undo selectmany:Pickup1,Pickup2 copy paste dumptree undo selectmany:Pickup3,Pickup4 del dumptree undo \
  reparent:Pickup5\|\|Ground reparent:Ground\|Ground \
  selectmany:Camera,Platform2,Pickup1 gizmos pickobj:Platform3 pickobj:Pickup2 pickobj:Light pickobj:Camera boxsel:300,110,1200,610 dumpsel > "$WORK/log3d.txt" 2>&1
block "$WORK/log3d.txt" 1 | grep -q "^    Platform2$"                    # under Ground
block "$WORK/log3d.txt" 2 | grep -q "^  Platform2$"                       # one undo undoes all three
b3=$(block "$WORK/log3d.txt" 3 | wc -l); b2=$(block "$WORK/log3d.txt" 2 | wc -l)
[ "$b3" -eq $((b2 + 2)) ]                                                 # duplicate of two objects
b4=$(block "$WORK/log3d.txt" 4 | wc -l); [ "$b4" -eq $((b2 + 2)) ]       # copy + paste of two objects
b5=$(block "$WORK/log3d.txt" 5 | wc -l); [ "$b5" -eq $((b2 - 2)) ]       # delete of two objects
grep -q "reparent refused" "$WORK/log3d.txt"                              # Ground under itself
grep -q "pickobj Platform3 -> Platform3" "$WORK/log3d.txt"
grep -q "pickobj Pickup2 -> Pickup2" "$WORK/log3d.txt"
grep -q "pickobj Light -> Light" "$WORK/log3d.txt"
grep -q "pickobj Camera -> Camera" "$WORK/log3d.txt"
grep -q "gizmos lines=[1-9][0-9]* .* mode=3d" "$WORK/log3d.txt"
n=$(sed -n 's/^scene: boxsel -> \([0-9]*\) object.*/\1/p' "$WORK/log3d.txt"); [ "${n:-0}" -ge 5 ]
[ -s "$WORK/scene3d.png" ]
echo "scene_ui_check: ok"
