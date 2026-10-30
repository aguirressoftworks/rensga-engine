#!/bin/bash
# API 11 exercise "patrol script with gizmos": the Patrol script's OnDrawGizmos / OnDrawGizmosSelected waypoints show in the scene
# view. Skipped (77) until samples/editor_tour has Assets/Scripts/Patrol.cpp and a scene named Patrol.
# usage: exercise_patrol.sh <rensga-editor> <samples/editor_tour> <work dir>
. "$(dirname "$0")/exercise_lib.sh"
[ -f "$2/Assets/Scripts/Patrol.cpp" ] && [ -f "$2/Assets/Scenes/Patrol.scene" ] || { echo "skip: no Patrol script/scene in $2"; exit 77; }
ex_init "$1" "$2" "$3"
ex_run "$WORK/log.txt" waitbuild openscene:Patrol mode:3d select:Patrol framesel gizmos "shot:$WORK/patrol_gizmos.png" quit
[ $EX_RC -eq 0 ] || ex_fail "editor exit $EX_RC"
grep -q "scripts reloaded (generation" "$LOG" || ex_fail "the script module did not load"
grep -q "gizmos lines=[1-9][0-9]* .*mode=3d" "$LOG" || ex_fail "no gizmo lines in the scene view"
[ -s "$WORK/patrol_gizmos.png" ] || ex_fail "no screenshot"
echo "exercise_patrol: ok"
