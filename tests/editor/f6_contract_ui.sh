#!/bin/sh
# F6 E0: the registry-driven editor still runs --ui-event verbs, the Project window's Create menu offers the F6 asset kinds,
# assetcreate: makes real files, and the imnodes smoke window draws. Exit 77 = skipped (no xvfb-run).
# usage: f6_contract_ui.sh <rensga-editor> <sample dir> <work dir>
EDITOR=$1; SAMPLE=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
set -e
rm -rf "$WORK"; mkdir -p "$WORK/proj" "$WORK/cfg"
cp -r "$SAMPLE/Assets" "$SAMPLE/ProjectSettings" "$WORK/proj/"
rm -rf "$WORK/proj/Assets/Scripts"
run() { out=$1; shift; A=""; for e in "$@"; do A="$A --ui-event $e"; done
  env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" xvfb-run -a -s "-screen 0 1600x900x24" \
    "$EDITOR" --config-dir "$WORK/cfg" --project "$WORK/proj" --no-splash --no-auto-rebuild --focus Project \
    --screenshot "$out" --screenshot-frames 120 $A; }
run "$WORK/created.png" folder:Assets assetcreate:Prefab assetcreate:Material assetcreate:Animator_Controller assetcreate:Property_Clip assetcreate:Data_Asset nodesmoke > "$WORK/log1.txt" 2>&1
for f in "New Prefab.prefab" "New Material.mat.json" "New Animator Controller.controller.json" "New Property Clip.clip.json" "New Data Asset.asset.json"; do
  [ -f "$WORK/proj/Assets/$f" ] && [ -f "$WORK/proj/Assets/$f.meta" ] || { echo "missing $f"; cat "$WORK/log1.txt"; exit 1; }
done
grep -q "asset: created Assets/New Prefab.prefab (prefab)" "$WORK/log1.txt"
grep -q "asset: created Assets/New Material.mat.json (material)" "$WORK/log1.txt"
grep -q "nodes: smoke window open" "$WORK/log1.txt"
grep -q '"rootFileId"' "$WORK/proj/Assets/New Prefab.prefab"
run "$WORK/menu.png" folder:Assets createmenu > "$WORK/log2.txt" 2>&1
[ -s "$WORK/created.png" ] && [ -s "$WORK/menu.png" ]
echo "f6_contract_ui: ok"
