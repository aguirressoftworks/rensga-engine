#!/bin/sh
# F3.02: Project window, asset inspector and VRAM map under xvfb, on a temp copy of samples/platformer2d (a project whose
# target has no TargetModule). The exercise "import a texture": a dropped PNG gets a .meta, its ps2 palette depth override
# goes 8 -> 4 bit, the asset is reimported. Exit 77 = skipped (no xvfb-run).
# usage: assets_ui_check.sh <rensga-editor> <sample dir> <png fixture> <work dir>
EDITOR=$1; SAMPLE=$2; PNG=$3; WORK=$4
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
set -e
rm -rf "$WORK"; mkdir -p "$WORK/proj" "$WORK/cfg"
cp -r "$SAMPLE/Assets" "$SAMPLE/ProjectSettings" "$WORK/proj/"
rm -rf "$WORK/proj/Assets/Scripts"           # no scripts: no script module build
cp "$PNG" "$WORK/proj/Assets/Textures/palette12.png"   # dropped into Assets from outside
run() { out=$1; shift; A=""; for e in "$@"; do A="$A --ui-event $e"; done
  env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" xvfb-run -a -s "-screen 0 1600x900x24" \
    "$EDITOR" --config-dir "$WORK/cfg" --project "$WORK/proj" --no-splash --no-auto-rebuild --focus Project \
    --screenshot "$out" --screenshot-frames 90 $A; }
run "$WORK/project.png" asset:Assets/Textures/palette12.png search:t:texture vram > "$WORK/log1.txt" 2>&1
grep -q "no editor module" "$WORK/log1.txt"
[ -s "$WORK/project.png" ]
META="$WORK/proj/Assets/Textures/palette12.png.meta"
[ -f "$META" ] && grep -q '"guid"' "$META"     # auto-imported with a .meta
run "$WORK/import.png" asset:Assets/Textures/palette12.png assettab:ps2 assetset:format=clut8 assetapply assetset:format=clut4 assetapply > "$WORK/log2.txt" 2>&1
grep -q "asset: reimported Assets/Textures/palette12.png" "$WORK/log2.txt"
grep -q '"clut4"' "$META" && grep -q '"targetOverrides"' "$META"
[ -s "$WORK/import.png" ]
echo "assets_ui_check: ok"
