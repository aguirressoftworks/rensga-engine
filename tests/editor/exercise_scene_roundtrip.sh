#!/bin/bash
# Editor 2 exercise "save and reopen a scene": build a three-level nested scene in editor_tour, save, reopen from disk, save
# again: the two files are byte-identical and the reopened hierarchy equals the saved one.
# usage: exercise_scene_roundtrip.sh <rensga-editor> <samples/editor_tour> <work dir>
. "$(dirname "$0")/exercise_lib.sh"
ex_init "$1" "$2" "$3"
ex_run "$WORK/log.txt" waitbuild openscene:Tour new:Nest1 new:Nest2\|Nest1 new:Nest3\|Nest2 \
  selectmany:Orbiter reparent:Orbiter\|Nest3 dumptree save savecopy:"$WORK/s1.scene" reload dumptree \
  savecopy:"$WORK/s2.scene" select:Spinner "shot:$WORK/hierarchy.png" quit
[ $EX_RC -eq 0 ] || ex_fail "editor exit $EX_RC"
cmp "$WORK/s1.scene" "$WORK/s2.scene" || ex_fail "saved, reopened, saved again: the files differ"
block() { awk -v n="$1" '/^scene: tree/{c++; next} /^scene: /{if(c==n)exit} c==n{print}' "$LOG" | grep -v '^\[editor\]' | sed 's/ \*$//'; }
block 1 | grep -q "^      Nest3$" || ex_fail "Nest3 is not three levels deep"
block 1 | grep -q "^        Orbiter$" || ex_fail "Orbiter is not under Nest3"
block 1 | grep -q "^          Moon$" || ex_fail "the Moon child did not follow Orbiter"
block 1 > "$WORK/tree1.txt"; block 2 > "$WORK/tree2.txt"
diff "$WORK/tree1.txt" "$WORK/tree2.txt" || ex_fail "the reopened hierarchy differs"
python3 - "$WORK/proj/Assets/Scenes/Tour.scene" <<'PY' || exit 1
import json, sys
j = json.load(open(sys.argv[1]))
by = {o["guid"]: o for o in j["objects"]}
def depth(o):
    d = 0
    while o["parent"]: o = by[o["parent"]]; d += 1
    return d
names = {o["name"]: depth(o) for o in j["objects"]}
assert names["Nest1"] == 0 and names["Nest2"] == 1 and names["Nest3"] == 2 and names["Orbiter"] == 3 and names["Moon"] == 4, names
PY
[ -s "$WORK/hierarchy.png" ] || ex_fail "no screenshot"
echo "exercise_scene_roundtrip: ok"
