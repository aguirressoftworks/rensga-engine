#!/bin/sh
# F3.04 / gate F3.G3: Play in editor under xvfb on the repo's real projects. Opens the project, presses Play (in editor), pauses,
# steps exact frames while scripts move the objects, changes a field and moves an object during play, takes Game view
# screenshots, presses Stop and checks that the scene file and the document are byte-identical and not dirty.
# Exit 77 = skipped (no xvfb-run / editor binary).
# usage: play_ui_check.sh <rensga-editor> <project dir> <scene name> <object> <keys> <field-spec> <work dir>
#   field-spec: <component index>|<field>|<json value>   (set during play on <object>)
EDITOR=$1; PROJ=$2; SCENE=$3; OBJ=$4; KEYS=$5; FIELD=$6; WORK=$7
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
[ -d "$PROJ" ] || { echo "skip: no project $PROJ"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK/cfg"
SCENEFILE=$(find "$PROJ/Assets" -name "$SCENE.scene" | head -1)
[ -f "$SCENEFILE" ] || { echo "no scene $SCENE"; exit 1; }
BEFORE=$(sha256sum "$SCENEFILE" | cut -d' ' -f1)
export RENSGA_JOBS=${RENSGA_JOBS:-2}
set -e
env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 RENSGA_HOME="$WORK/cfg" xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR" --config-dir "$WORK/cfg" --project "$PROJ" --no-splash --no-auto-rebuild --focus Game \
  --screenshot "$WORK/after_stop.png" --screenshot-frames 400 \
  --ui-event "openscene:$SCENE" --ui-event "select:$OBJ" --ui-event showgame \
  --ui-event playedit --ui-event "playkeys:$KEYS" --ui-event playwait:40 \
  --ui-event "playdump:$OBJ" --ui-event "shot:$WORK/play_running.png" \
  --ui-event pause --ui-event "playdump:$OBJ" --ui-event step:30 --ui-event "playdump:$OBJ" \
  --ui-event "playset:$OBJ|$FIELD" --ui-event "playmove:$OBJ|3,2,1" --ui-event step:2 --ui-event "playdump:$OBJ|$FIELD" \
  --ui-event "shot:$WORK/play_paused.png" \
  --ui-event stopedit --ui-event "shot:$WORK/stopped.png" > "$WORK/log.txt" 2>&1 || { cat "$WORK/log.txt"; exit 1; }
AFTER=$(sha256sum "$SCENEFILE" | cut -d' ' -f1)
[ "$BEFORE" = "$AFTER" ] || { echo "scene file changed during play"; exit 1; }
grep -q "^play: started" "$WORK/log.txt" || { cat "$WORK/log.txt"; echo "play did not start"; exit 1; }
grep -q "^play-check: .*identical=1 dirty=0 dirty_before=0 undo=0 undo_before=0 selected_same=1" "$WORK/log.txt" || { cat "$WORK/log.txt"; echo "stop did not restore"; exit 1; }
grep -q "^playset: .*applied" "$WORK/log.txt" && grep -q "^playmove: .*applied" "$WORK/log.txt" || { cat "$WORK/log.txt"; echo "edits refused"; exit 1; }
# the script moved the object between the first dump (running), the paused dump and after 30 steps
grep "^playdump:" "$WORK/log.txt" > "$WORK/dumps.txt"
P1=$(sed -n 2p "$WORK/dumps.txt" | sed 's/.*pos=\([^ ]*\).*/\1/'); P2=$(sed -n 3p "$WORK/dumps.txt" | sed 's/.*pos=\([^ ]*\).*/\1/')
[ "$P1" != "$P2" ] || { cat "$WORK/dumps.txt"; echo "30 steps did not move $OBJ"; exit 1; }
F1=$(sed -n 2p "$WORK/dumps.txt" | sed 's/.*fixed=\([0-9]*\).*/\1/'); F2=$(sed -n 3p "$WORK/dumps.txt" | sed 's/.*fixed=\([0-9]*\).*/\1/')
[ $((F2 - F1)) -eq 30 ] || { cat "$WORK/dumps.txt"; echo "step:30 ran $((F2 - F1)) fixed steps"; exit 1; }
[ -s "$WORK/play_running.png" ] && [ -s "$WORK/play_paused.png" ] && [ -s "$WORK/stopped.png" ]
echo "play_ui_check: ok ($PROJ $SCENE)"
