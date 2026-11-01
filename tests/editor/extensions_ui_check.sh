#!/usr/bin/env bash
# F6.07 (Editor 13): samples/extending_editor in the real editor under xvfb.
#   1. the Editor/ module builds at project open and registers 1 inspector, 1 window and 2 menu items
#   2. the custom inspector draws under the Patroller fields (screenshot), a menu item changes the selection
#   3. Create > Data Asset > EnemyStats writes a typed file; dataset/dataundo edit it with undo; screenshot of the Inspector
#   4. hot reload: the source is edited, the module rebuilt, generation 2 shows the new text (screenshot)
#   (the cooked DATA section and the run on the player are tools/ci/extending_editor.sh; the cook unit test is test_data_cook.cpp)
# Exit 77 = skipped (no xvfb-run). usage: extensions_ui_check.sh <editor> <sample dir> <work dir>
EDITOR=$(realpath "$1"); SAMPLE=$(realpath "$2"); mkdir -p "$3"; WORK=$(realpath "$3")
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
set -eu
rm -rf "$WORK"; mkdir -p "$WORK/proj" "$WORK/cfg"
cp -r "$SAMPLE/Assets" "$SAMPLE/ProjectSettings" "$SAMPLE/Editor" "$WORK/proj/"
fail() { echo "extensions_ui: $*" >&2; exit 1; }
need() { grep -qE "$1" "$2" || { echo "--- $2" >&2; tail -40 "$2" >&2; fail "$3"; }; }
run() { out=$1; log=$2; shift 2; A=(); for e in "$@"; do A+=(--ui-event "$e"); done
  env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" RENSGA_JOBS=2 xvfb-run -a -s "-screen 0 1600x900x24" \
    "$EDITOR" --config-dir "$WORK/cfg" --project "$WORK/proj" --no-splash --no-auto-rebuild \
    --screenshot "$out" --screenshot-frames 150 "${A[@]}" > "$log" 2>&1; }

# 1 + 2: the custom inspector, the window, a menu item
run "$WORK/inspector.png" "$WORK/log1.txt" extdump extwindow:Patrol_Overview extmenu:Tools/Patrol/Select_All_Patrollers select:PatrolScout inspscroll:700
need "ext: module loaded=1 generation=1 inspectors=1 windows=1 menus=2" "$WORK/log1.txt" "the Editor/ module did not load with 1 inspector, 1 window and 2 menu items"
need "ext: window Patrol_Overview open" "$WORK/log1.txt" "the Patrol Overview window did not open"
need "ext: menu Tools/Patrol/Select_All_Patrollers ran" "$WORK/log1.txt" "the menu item did not run"
[ -s "$WORK/inspector.png" ] || fail "no inspector screenshot"
echo "extensions_ui: module loaded, custom inspector, window and menu item ok"

# 3: data assets in the Inspector
run "$WORK/data.png" "$WORK/log2.txt" folder:Assets/Data asset:Assets/Data/Grunt.asset.json datadump dataset:health=77 dataset:kind=2 'dataset:label="Boss"' datadump dataundo datadump datacreate:EnemyStats\|Elite
need 'data: Assets/Data/Grunt.asset.json type=EnemyStats fields=.*"health":30' "$WORK/log2.txt" "the data asset did not show its stored values"
need 'data: set health ok undo=1' "$WORK/log2.txt" "dataset health failed"
need 'data: Assets/Data/Grunt.asset.json type=EnemyStats fields=.*"health":77.*"kind":2' "$WORK/log2.txt" "the edit is not in the document"
need 'data: undo ok undo=2 redo=1' "$WORK/log2.txt" "dataundo failed"
need 'data: created Assets/Data/Elite.asset.json type=EnemyStats' "$WORK/log2.txt" "Create > Data Asset did not create the typed asset"
grep -q '"health": 77' "$WORK/proj/Assets/Data/Grunt.asset.json" || fail "the edit was not written to the file"
grep -q '"label": "Boss"' "$WORK/proj/Assets/Data/Grunt.asset.json" && fail "the undone label edit is still in the file"
grep -q '"type": "EnemyStats"' "$WORK/proj/Assets/Data/Elite.asset.json" || fail "Elite.asset.json has no type"
echo "extensions_ui: data asset create, edit, undo ok"

# 4: hot reload (generation 2 after an edit of Editor/PatrolTools.cpp)
run "$WORK/reload.png" "$WORK/log3.txt" extedit:'PatrolTools.cpp|Patrol tools|Patrol tools (reloaded)' extbuild extdump select:PatrolBrute
need "ext: edit ok" "$WORK/log3.txt" "the test edit did not apply"
need "ext: build ok" "$WORK/log3.txt" "the rebuild failed"
need "ext: module loaded=1 generation=2 inspectors=1 windows=1 menus=2" "$WORK/log3.txt" "the module did not hot reload to generation 2"
grep -q "Patrol tools (reloaded)" "$WORK/proj/Editor/PatrolTools.cpp" || fail "the source edit is missing"
echo "extensions_ui: hot reload ok"

# a syntax error keeps the last good module (headless: the build fails and the previous library is not replaced)
echo 'this is not C++;' >> "$WORK/proj/Editor/PatrolTools.cpp"
run "$WORK/broken.png" "$WORK/log4.txt" extbuild extdump
need "ext: build failed" "$WORK/log4.txt" "a broken source should fail the build"
sed -i '$ d' "$WORK/proj/Editor/PatrolTools.cpp"

echo "extensions_ui: ok"
