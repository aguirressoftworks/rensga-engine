#!/bin/bash
# Editor 4 exercise "hot-reload a Rotator": Rotator (speed field) on Spinner in editor_tour. Play in editor, set speed in the
# inspector (playset), edit the rotation formula in the source while the editor runs: the background build finishes, the module
# reloads, the object keeps its rotation and the inspector-set speed, and from then on turns around X instead of Y.
# usage: exercise_hot_reload.sh <rensga-editor> <samples/editor_tour> <work dir>
. "$(dirname "$0")/exercise_lib.sh"
ex_init "$1" "$2" "$3"
F=Assets/Scripts/Rotator.cpp
OLD='Quaternion::Euler(0, speed * Time::deltaTime(), 0)'
NEW='Quaternion::Euler(speed * Time::deltaTime(), 0, 0)'
grep -q -F "$OLD" "$WORK/proj/$F" || ex_fail "the formula in $F changed; update this test"
ex_run "$WORK/log.txt" waitbuild openscene:Tour showgame playedit "playset:Spinner|1|speed|90" playwait:30 pause \
  "playdump:Spinner|1|speed" "shot:$WORK/before_reload.png" \
  "edit:$F|$OLD|$NEW" waitbusy waitbuild "playdump:Spinner|1|speed" step:20 "playdump:Spinner|1|speed" \
  "shot:$WORK/after_reload.png" stopedit quit
[ $EX_RC -eq 0 ] || ex_fail "editor exit $EX_RC"
grep -q "scripts reloaded (generation" "$LOG" || ex_fail "no script reload"
grep -q "play: scripts reloaded; running state kept" "$LOG" || ex_fail "the play session did not rebuild its World"
grep "^playdump:" "$LOG" > "$WORK/dumps.txt"
[ "$(wc -l < "$WORK/dumps.txt")" -eq 3 ] || ex_fail "expected three playdump lines"
rot() { sed -n "$1p" "$WORK/dumps.txt" | sed 's/.* rot=\([^ ]*\) .*/\1/'; }
R1=$(rot 1); R2=$(rot 2); R3=$(rot 3)
[ "$R1" = "$R2" ] || ex_fail "the rotation changed across the reload ($R1 -> $R2)"
[ "$(grep -c 'Rotator.speed=90' "$WORK/dumps.txt")" -eq 3 ] || ex_fail "the speed set in the inspector did not survive"
python3 - "$R1" "$R2" "$R3" <<'PY' || ex_fail "the new formula is not active"
import sys
q1, q2, q3 = [[float(v) for v in a.split(",")] for a in sys.argv[1:4]]
assert abs(q1[0]) < 1e-4 and abs(q1[1]) > 1e-3, ("before: turning around Y", q1)   # old formula: x stays 0
assert q3 != q2 and abs(q3[0]) > 1e-3, ("after: turning around X", q3)             # new formula: x grows
PY
[ -s "$WORK/before_reload.png" ] && [ -s "$WORK/after_reload.png" ] || ex_fail "no screenshots"
echo "exercise_hot_reload: ok ($R1 -> $R3)"
