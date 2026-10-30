#!/bin/bash
# API 4 exercise "coroutine door": the Door opens over 2 s with a coroutine and fires OnOpened. The event is wired to Bell's
# DoorBell.Ring in the inspector (UnityEvent drawer model, driven with setfield), saved, then the scene plays in the editor:
# half way the door is still rising and the bell has not rung; after 2 s it rang exactly once.
# usage: exercise_door.sh <rensga-editor> <samples/editor_tour> <work dir>
. "$(dirname "$0")/exercise_lib.sh"
ex_init "$1" "$2" "$3"
BELL=00000000-0000-4000-9000-000000000215
EVT='[{"target":"'$BELL'","type":"DoorBell","method":"Ring"}]'
ex_run "$WORK/log.txt" waitbuild openscene:Door select:Door "setfield:Door.OnOpened=$EVT" inspscroll:400 "shot:$WORK/wired.png" save \
  showgame playedit pause step:60 "playdump:Door" "playdump:Bell|1|rings" step:90 "playdump:Door" "playdump:Bell|1|rings" \
  step:200 "playdump:Bell|1|rings" "shot:$WORK/opened.png" stopedit quit
[ $EX_RC -eq 0 ] || ex_fail "editor exit $EX_RC"
python3 - "$WORK/proj/Assets/Scenes/Door.scene" "$BELL" <<'PY' || ex_fail "the wired listener is not in the saved scene"
import json, sys
j = json.load(open(sys.argv[1]))
d = [c for o in j["objects"] if o["name"] == "Door" for c in o["components"] if c["type"] == "Door"][0]
ev = d["fields"]["OnOpened"]
assert len(ev) == 1 and ev[0]["target"] == sys.argv[2] and ev[0]["method"] == "Ring", ev
PY
grep "^playdump:" "$LOG" > "$WORK/dumps.txt"
[ "$(wc -l < "$WORK/dumps.txt")" -eq 5 ] || ex_fail "expected five playdump lines"
y() { sed -n "$1p" "$WORK/dumps.txt" | sed 's/.*pos=[^,]*,\([^,]*\),.*/\1/'; }
rings() { sed -n "$1p" "$WORK/dumps.txt" | sed 's/.*rings=\([0-9]*\).*/\1/'; }
[ "$(rings 2)" = "0" ] || ex_fail "the bell rang before the door opened"
python3 -c "import sys; y=float(sys.argv[1]); assert 1.2 < y < 3.8, y" "$(y 1)" || ex_fail "the door is not half way up after 60 frames ($(y 1))"
[ "$(rings 4)" = "1" ] || ex_fail "after 2 s the bell rang $(rings 4) times, expected once"
python3 -c "import sys; y=float(sys.argv[1]); assert abs(y-4.0) < 1e-3, y" "$(y 3)" || ex_fail "the door did not finish at the top ($(y 3))"
[ "$(rings 5)" = "1" ] || ex_fail "the event fired again later (rings=$(rings 5))"
[ -s "$WORK/wired.png" ] && [ -s "$WORK/opened.png" ] || ex_fail "no screenshots"
echo "exercise_door: ok"
