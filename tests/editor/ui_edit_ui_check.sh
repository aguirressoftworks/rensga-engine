#!/bin/bash
# F6.04 Editor 10: the canvas in the Scene view (create menu objects, move and resize with the rect handles, undo), the Input Actions window and
# the Project Settings window, all through --ui-event verbs under xvfb, with screenshots. Exit 77 = skipped (no xvfb-run / python3 / editor).
# usage: ui_edit_ui_check.sh <rensga-editor> <samples/menus> <work dir>
EDITOR_BIN="$1"; SAMPLE="$2"; WORK="$3"
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
command -v python3 >/dev/null 2>&1 || { echo "skip: no python3"; exit 77; }
[ -x "$EDITOR_BIN" ] || { echo "skip: no editor binary"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK/proj" "$WORK/cfg"
cp -r "$SAMPLE/Assets" "$SAMPLE/ProjectSettings" "$WORK/proj/"
rm -rf "$WORK/proj/Assets/Scripts"          # the checks need no compiled scripts
python3 - "$WORK/proj" <<'PY'                # the sample's scenes keep script components the editor cannot show without the module: drop them for this check
import json, sys, glob
for f in glob.glob(sys.argv[1] + "/Assets/Scenes/*.scene"):
    j = json.load(open(f))
    for o in j["objects"]:
        o["components"] = [c for c in o["components"] if c["type"] not in ("MenuFlow", "MenuBot")]
    json.dump(j, open(f, "w"), indent=1)
PY
fail() { echo "FAIL: $1"; tail -40 "$LOG"; exit 1; }
run() { LOG="$1"; shift; local args=() e; for e in "$@"; do args+=(--ui-event "$e"); done
  env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" xvfb-run -a -s "-screen 0 1600x900x24" \
    "$EDITOR_BIN" --config-dir "$WORK/cfg" --project "$WORK/proj" --no-splash --no-auto-rebuild --quit-after 3000 "${args[@]}" > "$LOG" 2>&1; }
grepq() { grep -q -- "$1" "$LOG" || fail "$2 (missing: $1)"; }

# ---- the canvas in the Scene view ----
run "$WORK/canvas.log" openscene:Title mode:2d framesel uidump "shot:$WORK/canvas-overview.png" \
  select:PlayButton uimove:PlayButton\|12,8 uiresize:PlayButton\|r\|20,0 uiresize:PlayButton\|b\|0,6 uidump \
  "shot:$WORK/canvas-handles.png" undo undo undo uidump quit
[ -s "$WORK/canvas-overview.png" ] || fail "no canvas screenshot"
grepq "ui: view 480x272 scale=1.000 virtual=480x272 canvas=1 reference=480x272" "the preview must lay the scene out at the target resolution"
grepq "ui: moved PlayButton x=-98 y=-22" "uimove moves by virtual pixels"
grepq "ui: resized PlayButton x=-88 y=-22 w=130 h=24" "the right handle widens the button and keeps its left edge"
grepq "ui: resized PlayButton x=-88 y=-19 w=130 h=30" "the bottom handle grows it downwards"
# after the three undos the button is back where the sample put it
awk '/^ui: view/{n++} n==3 && /PlayButton/' "$WORK/canvas.log" | grep -q "rect=75,94,110,24" || fail "undo did not restore the button (75,94,110,24)"

# the create menu objects, through the same code as GameObject > UI
run "$WORK/create.log" openscene:Game uicreate:Button\|\|ExtraButton uicreate:Slider\|\|ExtraSlider uicreate:Toggle\|\|ExtraToggle uicreate:Layout\|\|Column \
  uicreate:Image\|\|ExtraImage uicreate:Text\|\|ExtraText dumptree uidump "shot:$WORK/create.png" undo dumptree quit
grepq "ui: created Slider 'ExtraSlider'" "slider created"
grepq "ExtraSliderFill" "the slider has a fill child"
[ -s "$WORK/create.png" ] || fail "no create screenshot"

# ---- Input Actions ----
run "$WORK/input.log" inputactions iadump iaadd:button\|Fire iaset:Fire\|source=west iaset:Fire\|alt=key:LeftControl iaadd:axis2\|Aim iaset:Aim\|source=composite \
  iaset:Aim\|up=key:I iaset:Aim\|down=key:K iaset:Aim\|left=key:J iaset:Aim\|right=key:L iaadd:axis\|Zoom iaset:Zoom\|source=r2 iadump "shot:$WORK/input-actions.png" \
  iasave iaundo iaredo iadump iaset:Fire\|alt=key:NoSuchKey iadump quit
grepq "input: window open" "window opens"
grepq "input: added 'Fire' (button)" "add button"
grepq "input: 4 actions dirty=1 problems=0" "after the edits: 4 actions (Menu, Fire, Aim, Zoom), unsaved, the cooker accepts them"
grepq "input:   Aim kind=axis2 source=composite up=key:I down=key:K left=key:J right=key:L" "composite bindings"
grepq "input: save ok" "save"
grepq "problem: action 'Fire': 'alt': unknown binding 'key:NoSuchKey'" "the cooker's own check reports a bad key"
python3 - "$WORK/proj/ProjectSettings/InputActions.json" <<'PY' || fail "InputActions.json content"
import json, sys
j = json.load(open(sys.argv[1]))
names = [a["name"] for a in j["actions"]]
assert names == ["Menu", "Fire", "Aim", "Zoom"], names
fire = j["actions"][1]
assert fire["source"] == "west" and fire["alt"] == "key:LeftControl", fire
assert j["actions"][2]["up"] == "key:I" and j["actions"][3]["source"] == "r2", j["actions"]
PY
[ -s "$WORK/input-actions.png" ] || fail "no input actions screenshot"

# ---- Project Settings ----
run "$WORK/settings.log" projsettings:tags psset:tag.add=Enemy psset:tag.add=Pickup psset:layer.6=Player psset:layer.7=Enemy "shot:$WORK/settings-tags.png" \
  projsettings:physics psset:p3.collide.6.7=0 psset:p3.gravity=0,-12,0 "shot:$WORK/settings-physics.png" \
  projsettings:physics2d psset:p2.layer.add=Player psset:p2.layer.add=Enemy psset:p2.collide.1.2=0 "shot:$WORK/settings-physics2d.png" \
  projsettings:time psset:time.fixed=0.02 "shot:$WORK/settings-time.png" \
  projsettings:scenes psset:scene.enable.2=0 "shot:$WORK/settings-scenes.png" projsettings:video "shot:$WORK/settings-video.png" psdump pssave psdump quit
grepq "settings: tags=\[Enemy,Pickup\] layers=\[0=Default,1=TransparentFX,2=Ignore Raycast,4=Water,5=UI,6=Player,7=Enemy\]" "tags and layers"
grepq "settings: physics gravity=0,-12,0 triggers=1 ignored_pairs=1" "3D matrix: one ignored pair"
grepq "settings: physics2d layers=\[Default,Player,Enemy\] ignored_pairs=1" "2D matrix"
grepq "settings: time fixed=0.020000" "time"
grepq "settings: video 480x272 refresh=60" "video is read from the ConsoleTarget"
grepq "settings: save ok" "save"
python3 - "$WORK/proj/ProjectSettings" <<'PY' || fail "settings files"
import json, sys, os
d = sys.argv[1]
t = json.load(open(d + "/TagManager.json")); assert t["tags"] == ["Enemy", "Pickup"] and t["layers"][6] == "Player" and len(t["layers"]) == 32, t
p = json.load(open(d + "/Physics.json")); assert p["ignore"] == [[6, 7]] and p["gravity"][1] == -12, p
q = json.load(open(d + "/Physics2D.json")); assert q["layers"] == ["Default", "Player", "Enemy"] and q["ignore"] == [["Player", "Enemy"]], q
ti = json.load(open(d + "/Time.json")); assert abs(ti["fixedTimestep"] - 0.02) < 1e-9, ti
pr = json.load(open(d + "/Project.json")); assert len(pr["scenes"]) == 2 and len(pr["buildScenes"]) == 3, pr
PY
for f in settings-tags settings-physics settings-physics2d settings-time settings-scenes settings-video; do [ -s "$WORK/$f.png" ] || fail "no $f screenshot"; done
echo "ui_edit_ui_check: ok"
