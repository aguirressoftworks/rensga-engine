#!/bin/bash
# F6.01 exercise (Editor 6, "Prefabs"): make an Enemy-like prefab, place five instances, override one, change the prefab, and see that
# the override survives while the other four follow. Driven with --ui-event verbs under xvfb; screenshots of the hierarchy (blue
# instances), the inspector (override markers) and prefab edit mode (breadcrumbs) land in the work dir.
# Also checks the shipped sample (samples/prefab_enemies): variant, nested prefab, one override on the third enemy.
# usage: exercise_prefab_enemies.sh <rensga-editor> <samples/prefab_enemies> <work dir>      exit 77 = skipped
EDITOR_BIN="$1"; SAMPLE="$2"; WORK="$3"
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
command -v python3 >/dev/null 2>&1 || { echo "skip: no python3"; exit 77; }
[ -x "$EDITOR_BIN" ] || { echo "skip: no editor binary"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK/home"
cp -r "$SAMPLE" "$WORK/proj"; rm -f "$WORK/proj/make_sample.py"; rm -rf "$WORK/proj/Library"
export RENSGA_HOME="$WORK/home" RENSGA_JOBS=${RENSGA_JOBS:-2}
LOG="$WORK/log.txt"
fail() { echo "FAIL: $1"; tail -60 "$LOG"; exit 1; }
run() {
  local args=() e
  for e in "$@"; do args+=(--ui-event "$e"); done
  env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
    "$EDITOR_BIN" --project "$WORK/proj" --no-splash --no-auto-rebuild --config-dir "$WORK/home/cfg" --quit-after 2400 "${args[@]}" > "$LOG" 2>&1
}
CUBE=00000000-0000-4000-8000-00000000c001
run \
  prefabdump:Enemy3 prefabfield:Enemy3\|0\|color prefabfield:Enemy1\|0\|color prefabfield:Boss\|0\|color prefabfield:EnemyB\|0\|color \
  select:Enemy1 "shot:$WORK/hierarchy_instances.png" \
  select:Enemy3 "shot:$WORK/inspector_override.png" \
  new:Grunt select:Grunt addcomp:MeshRenderer "setfield:MeshRenderer.mesh=\"$CUBE\"" 'setfield:MeshRenderer.color=[60,160,220,255]' \
  new:Helmet\|Grunt select:Helmet addcomp:MeshRenderer "setfield:MeshRenderer.mesh=\"$CUBE\"" \
  prefabcreate:Grunt\|Assets/Prefabs \
  prefabinstance:Grunt\|\|-6,0,4 prefabinstance:Grunt\|\|-3,0,4 prefabinstance:Grunt\|\|0,0,4 prefabinstance:Grunt\|\|3,0,4 \
  prefabset:Grunt#4\|0\|color\|[250,200,40,255] \
  prefabfield:Grunt#4\|0\|color prefabfield:Grunt#2\|0\|color \
  prefabselect:Grunt#4 "shot:$WORK/five_instances.png" \
  prefabopen:Grunt select:Grunt 'setfield:MeshRenderer.color=[200,40,200,255]' prefabdump "shot:$WORK/prefab_mode.png" prefabsave prefabclose \
  prefabfield:Grunt#1\|0\|color prefabfield:Grunt#2\|0\|color prefabfield:Grunt#3\|0\|color prefabfield:Grunt#4\|0\|color prefabfield:Grunt#5\|0\|color \
  prefabselect:Grunt#4 "shot:$WORK/after_prefab_edit.png" \
  prefabrevert:Grunt#4\|0\|color prefabfield:Grunt#4\|0\|color undo prefabfield:Grunt#4\|0\|color \
  prefabrevert:Enemy3 \
  save quit
grep -q "prefab: instance Enemy3 asset=Assets/Prefabs/Enemy.prefab overrides=1 members=2" "$LOG" || fail "Enemy3 is not an instance with one override"
grep -q "prefab: field Enemy3 MeshRenderer.color = \[220,70,60,255\] overridden=1 prefab=\[90,190,90,255\]" "$LOG" || fail "Enemy3's override is not reported"
grep -q "prefab: field Enemy1 MeshRenderer.color = \[90,190,90,255\] overridden=0" "$LOG" || fail "Enemy1 is overridden"
grep -q "prefab: field Boss MeshRenderer.color = \[150,60,200,255\] overridden=0 prefab=\[150,60,200,255\]" "$LOG" || fail "the Boss variant does not carry its colour"
grep -q "prefab: field EnemyB MeshRenderer.color = \[90,120,220,255\]" "$LOG" || fail "the nested prefab's own override is lost"
grep -q "prefab: created Assets/Prefabs/Grunt.prefab" "$LOG" || fail "Grunt.prefab was not created"
grep -c "prefab: instance .* name=Grunt" "$LOG" | grep -q '^4$' || fail "expected four more Grunt instances"
grep -q "prefab: field Grunt MeshRenderer.color = \[250,200,40,255\] overridden=1 prefab=\[60,160,220,255\]" "$LOG" || fail "the fourth Grunt did not take the override"
grep -q 'prefab: stage level=1 prefab=Grunt .*breadcrumbs="Arena > Grunt"' "$LOG" || fail "prefab edit mode state is wrong"
# after editing the prefab: four follow (purple), the overridden one keeps its yellow
after=$(sed -n '/prefab: close ok/,$p' "$LOG" | grep "prefab: field Grunt MeshRenderer.color" | head -5)
[ "$(echo "$after" | grep -c '\[200,40,200,255\] overridden=0 prefab=\[200,40,200,255\]')" -eq 4 ] || fail "after the prefab edit the four plain instances do not all follow the new colour"
echo "$after" | grep -q '\[250,200,40,255\] overridden=1 prefab=\[200,40,200,255\]' || fail "the override did not survive the prefab edit"
python3 - "$WORK/proj" <<'PY' || fail "the saved files are wrong"
import json, os, sys
root = sys.argv[1]
scene = json.load(open(os.path.join(root, "Assets/Scenes/Arena.scene")))
grunt = json.load(open(os.path.join(root, "Assets/Prefabs/Grunt.prefab")))
meta = json.load(open(os.path.join(root, "Assets/Prefabs/Grunt.prefab.meta")))
assert grunt["guid"] == meta["guid"]
insts = [o for o in scene["objects"] if o.get("prefab") and o["prefab"]["asset"] == meta["guid"]]
assert len(insts) == 5, len(insts)
assert not any(o["name"] == "Helmet" for o in scene["objects"]), "the prefab's members must not be stored in the scene"
with_ov = [o for o in insts if o.get("overrides")]
assert len(with_ov) == 1 and with_ov[0]["overrides"][0]["path"] == "MeshRenderer.color", with_ov
enemy3 = [o for o in scene["objects"] if o["name"] == "Enemy3"][0]
assert not enemy3.get("overrides"), "Enemy3 was reverted by the last verb"
body = [o for o in grunt["objects"] if o["name"] == "Grunt"][0]
assert body["components"][0]["fields"]["color"] == [200, 40, 200, 255], body
PY
for f in hierarchy_instances inspector_override five_instances prefab_mode after_prefab_edit; do [ -s "$WORK/$f.png" ] || fail "no screenshot $f"; done
echo "exercise_prefab_enemies: ok"
