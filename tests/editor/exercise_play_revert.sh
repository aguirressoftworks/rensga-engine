#!/bin/bash
# Editor 5 exercise "play and revert" (gate F3.G3 on editor_tour): Play in editor, pause, step exactly 30 frames, change a field and
# move an object while playing, press Stop: the scene file and the document are exactly as before.
# usage: exercise_play_revert.sh <rensga-editor> <samples/editor_tour> <work dir>
. "$(dirname "$0")/exercise_lib.sh"
ex_init "$1" "$2" "$3"
SCENE="$WORK/proj/Assets/Scenes/Tour.scene"
BEFORE=$(sha256sum "$SCENE" | cut -d' ' -f1)
ex_run "$WORK/log.txt" waitbuild openscene:Tour select:Spinner showgame playedit playwait:20 pause "playdump:Spinner|1|speed" \
  step:30 "playdump:Spinner|1|speed" "playset:Spinner|1|speed|120" "playmove:Spinner|3,2,1" "playmove:Spinner|0,2,1" step:2 \
  "playdump:Spinner|1|speed" "shot:$WORK/paused.png" stopedit "shot:$WORK/stopped.png" quit
[ $EX_RC -eq 0 ] || ex_fail "editor exit $EX_RC"
[ "$BEFORE" = "$(sha256sum "$SCENE" | cut -d' ' -f1)" ] || ex_fail "the scene file changed during play"
grep -q "^play: started" "$LOG" || ex_fail "play did not start"
grep -q "^playset: .*applied" "$LOG" || ex_fail "the live field edit was refused"
grep -q "^play-check: .*identical=1 dirty=0 dirty_before=0 undo=0 undo_before=0 selected_same=1" "$LOG" || ex_fail "Stop did not restore the scene"
grep "^playdump:" "$LOG" > "$WORK/dumps.txt"
F1=$(sed -n 1p "$WORK/dumps.txt" | sed 's/.*fixed=\([0-9]*\).*/\1/'); F2=$(sed -n 2p "$WORK/dumps.txt" | sed 's/.*fixed=\([0-9]*\).*/\1/')
[ $((F2 - F1)) -eq 30 ] || ex_fail "step:30 ran $((F2 - F1)) fixed steps"
[ "$(sed -n 1p "$WORK/dumps.txt" | sed 's/.* rot=\([^ ]*\) .*/\1/')" != "$(sed -n 2p "$WORK/dumps.txt" | sed 's/.* rot=\([^ ]*\) .*/\1/')" ] \
  || ex_fail "30 steps did not turn the Spinner"
grep -q "Rotator.speed=120" "$WORK/dumps.txt" || ex_fail "the live speed edit did not reach the World"
[ -s "$WORK/paused.png" ] && [ -s "$WORK/stopped.png" ] || ex_fail "no screenshots"
echo "exercise_play_revert: ok"
