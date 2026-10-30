#!/bin/sh
# F3.01 / F3.07 in the real editor under xvfb, on a temp copy of tests/fixtures/inspector_demo (a scene whose Player and Head
# carry a script with every field type, the attributes and a UnityEvent):
#   run 1: the script module builds in the background, the inspector shows every field type (screenshot), an OnValidate clamp,
#          Reset() and Move Up run through the same model calls as the menu, a two-object multi-selection shows mixed values;
#          the saved scene must hold the Reset() value and the new component order.
#   run 2: the same project with the script missing from the ConsoleTarget list: the component is greyed with its reason.
# usage: inspector_ui_check.sh <rensga-editor> <fixture dir> <out dir for screenshots>. Exit 77 = skipped.
EDITOR_BIN="$1"; FIXTURE="$2"; OUTDIR="$3"
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
command -v python3 >/dev/null 2>&1 || { echo "skip: no python3"; exit 77; }
[ -x "$EDITOR_BIN" ] || { echo "skip: no editor binary"; exit 77; }
T=$(mktemp -d "${TMPDIR:-/tmp}/rensga-insp.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
mkdir -p "$OUTDIR" "$T/home"
export RENSGA_HOME="$T/home" RENSGA_JOBS=2
run_editor() {  # <project> <log> <events...>
  P="$1"; L="$2"; shift 2
  set -- "$EDITOR_BIN" --project "$P" --no-splash --config-dir "$T/home/.config/rensga" --auto-rebuild --quit-after 1500 "$@"
  env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" "$@" > "$L" 2>&1
}
fail() { echo "FAIL: $1"; tail -40 "$2"; exit 1; }

cp -r "$FIXTURE" "$T/p1"
run_editor "$T/p1" "$T/run1.log" \
  --ui-event waitbuild --ui-event select:Player \
  --ui-event "shot:$OUTDIR/inspector_fields.png" \
  --ui-event inspscroll:5000 --ui-event "shot:$OUTDIR/inspector_unityevent.png" \
  --ui-event 'setfield:InspectorDemo.lives=-5' --ui-event resetcomp:3 --ui-event moveup:3 \
  --ui-event 'multiselect:Player|Head' --ui-event inspscroll:0 --ui-event "shot:$OUTDIR/inspector_multi.png" \
  --ui-event select:Player --ui-event save --ui-event quit || fail "editor 1 exit" "$T/run1.log"
grep -q "scripts reloaded (generation" "$T/run1.log" || fail "script module did not load" "$T/run1.log"
python3 - "$T/p1/Assets/Scenes/TestLevel.scene" <<'PY' || exit 1
import json, sys
j = json.load(open(sys.argv[1]))
p = [o for o in j["objects"] if o["name"] == "Player"][0]
types = [c["type"] for c in p["components"]]
assert types == ["MeshRenderer", "CharacterController", "InspectorDemo", "PlayerController"], types  # Move Up
d = [c for c in p["components"] if c["type"] == "InspectorDemo"][0]["fields"]
assert d["lives"] == 7, d  # Reset() ran after the OnValidate clamp (-5 -> 0)
assert d["onDone"] == [] and d["speed"] == 5, d  # Reset restores every field, listeners included
print("inspector_ui: saved scene ok")
PY
for f in inspector_fields inspector_unityevent inspector_multi; do [ -s "$OUTDIR/$f.png" ] || fail "no screenshot $f" "$T/run1.log"; done

# run 2: greyed component
cp -r "$FIXTURE" "$T/p2"
cp -r "$T/p1/Library" "$T/p2/Library" 2>/dev/null
python3 - "$T/p2/ProjectSettings/ConsoleTarget.json" <<'PY' || exit 1
import json, sys
c = json.load(open(sys.argv[1]))
c["allowedComponents"].remove("InspectorDemo")
json.dump(c, open(sys.argv[1], "w"), indent=2)
PY
run_editor "$T/p2" "$T/run2.log" --ui-event waitbuild --ui-event select:Player --ui-event inspscroll:380 \
  --ui-event "shot:$OUTDIR/inspector_greyed.png" --ui-event quit || fail "editor 2 exit" "$T/run2.log"
[ -s "$OUTDIR/inspector_greyed.png" ] || fail "no greyed screenshot" "$T/run2.log"
echo "inspector_ui: ok"
