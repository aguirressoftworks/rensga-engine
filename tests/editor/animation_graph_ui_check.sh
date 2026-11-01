#!/bin/sh
# F6.03 (Editor 9): the Animator graph window under xvfb on samples/animator_player: edits a copy of the Player controller (state,
# parameter, transition with conditions, motion), saves it, then plays in the editor and reads the live state of the running Animator.
# Exit 77 = skipped (no xvfb-run / editor binary).
# usage: animation_graph_ui_check.sh <rensga-editor> <project dir> <work dir>
EDITOR=$1; SAMPLE=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
[ -d "$SAMPLE/Assets" ] || { echo "skip: no project $SAMPLE"; exit 77; }
set -e
rm -rf "$WORK"; mkdir -p "$WORK/proj" "$WORK/cfg"
cp -r "$SAMPLE/Assets" "$SAMPLE/ProjectSettings" "$WORK/proj/"
CTL=Assets/Animation/Player.controller.json
run() { out=$1; log=$2; shift 2; A=""; for e in "$@"; do A="$A --ui-event $e"; done
  env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" xvfb-run -a -s "-screen 0 1600x900x24" \
    "$EDITOR" --config-dir "$WORK/cfg" --project "$WORK/proj" --no-splash --no-auto-rebuild --focus Scene \
    --screenshot "$out" --screenshot-frames 150 $A > "$log" 2>&1 || { cat "$log"; exit 1; }; }
need() { grep -qE "$1" "$2" || { cat "$2"; echo "missing: $1 ($3)"; exit 1; }; }

run "$WORK/graph.png" "$WORK/log1.txt" animatorgraph "animctl:$CTL" animctldump \
  "animstate:add|Land" "animparam:add|grounded|bool|1" "animtrans:Jump|Land|grounded" "animtransset:4|duration=0.12" \
  "animstate:motion|Land|Assets/Animation/Idle.clip.json" "animstate:opts|Land|loop=0|speed=1.5" "animtrans:Land|Idle|" animctldump animctlundo animctlredo animctlsave
need "^animator: controller=$CTL states=3 params=2 transitions=4 default=Idle dirty=0 valid=ok" "$WORK/log1.txt" "initial controller"
need "^animator:   trans 4 Jump -> Land exit=- duration=0.12 \[grounded\]" "$WORK/log1.txt" "new transition with its condition"
need "^animator: saved $CTL" "$WORK/log1.txt" "saved"
python3 -I - "$WORK/proj/$CTL" <<'PY'
import json, sys
c = json.load(open(sys.argv[1]))
assert [s["name"] for s in c["states"]] == ["Idle", "Run", "Jump", "Land"], c["states"]
land = c["states"][3]
assert land["loop"] is False and land["speed"] == 1.5 and land["motion"], land
assert any(p["name"] == "grounded" and p["type"] == "bool" and p["default"] is True for p in c["parameters"])
t = [t for t in c["transitions"] if t["from"] == "Jump" and t["to"] == "Land"][0]
assert t["conditions"] == [{"param": "grounded", "mode": "if"}] and abs(t["duration"] - 0.12) < 1e-9, t
PY

# a controller the editor wrote still cooks
CK=$(dirname "$EDITOR")/../../tools/cooker/rensga-cooker
[ -x "$CK" ] || CK=$(dirname "$EDITOR")/../../../tools/cooker/rensga-cooker
if [ -x "$CK" ]; then
  # this cooker is not built with the project's scripts (hook row F6.05), so cook a copy of the scene without the bot
  python3 -I - "$WORK/proj/Assets/Scenes/Main.scene" <<'PY'
import json, sys
s = json.load(open(sys.argv[1]))
for o in s["objects"]:
    o["components"] = [c for c in o["components"] if c["type"] != "PlayerBot"]
json.dump(s, open(sys.argv[1], "w"), indent=1)
PY
  "$CK" cook --project "$WORK/proj" --target linux --force > "$WORK/cook.txt" 2>&1 || { cat "$WORK/cook.txt"; echo "the edited controller does not cook"; exit 1; }
fi

# the live highlight: Play in the editor and ask the running Animator which state it is in
cp "$SAMPLE/Assets/Animation/Player.controller.json" "$WORK/proj/$CTL"
cp "$SAMPLE/Assets/Scenes/Main.scene" "$WORK/proj/Assets/Scenes/Main.scene"
run "$WORK/live.png" "$WORK/log2.txt" animatorgraph "animctl:$CTL" showgame playedit playwait:100 animlive "shot:$WORK/live_shot.png" animlive stopedit
need "^animator: live state=" "$WORK/log2.txt" "live state while playing"
[ -s "$WORK/graph.png" ] && [ -s "$WORK/live.png" ]
echo "animation_graph_ui_check: ok"
