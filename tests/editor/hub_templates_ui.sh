#!/bin/sh
# F4.09 / F4.10 in the real editor under xvfb (temp RENSGA_HOME, so nothing of the user's is touched):
#   1. the Create Project dialog with its six template cards (3 selectable, 3 greyed out "Coming in <phase>"): screenshots and the
#      hub-dump lines; hub-api:psx and hub-template on a greyed-out card are refused
#   2. for psp-2d and ps2-2d: Create via the dialog's code path, the editor opens the new project, its scripts build, Play in editor
#      runs the room (the player moves), Stop restores the scene (identical, not dirty)
# usage: hub_templates_ui.sh <rensga-editor> <work dir>. Exit 77 = skipped (no xvfb / editor).
EDITOR=$1; WORK=$2
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK"
export RENSGA_JOBS=${RENSGA_JOBS:-2}
fail() { echo "FAIL: $1"; [ -n "$2" ] && tail -60 "$2"; exit 1; }
xv() { env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" "$@"; }

# ---- 1. the dialog ----
H="$WORK/home1"; mkdir -p "$H/.config/rensga"
RENSGA_HOME="$H" xv "$EDITOR" --no-splash --config-dir "$H/.config/rensga" --quit-after 120 \
  --ui-event hub-dump --ui-event hub-new --ui-event "shot:$WORK/dialog_psp.png" \
  --ui-event hub-api:ps2 --ui-event hub-template:3d --ui-event hub-template:2d --ui-event "shot:$WORK/dialog_ps2.png" \
  --ui-event hub-api:psx --ui-event hub-dump \
  --ui-event hub-api:psp --ui-event hub-template:2d --ui-event "shot:$WORK/dialog_psp_2d.png" --ui-event hub-dump --ui-event quit \
  > "$WORK/dialog.txt" 2>&1 || fail "dialog run exit $?" "$WORK/dialog.txt"
D="$WORK/dialog.txt"
for l in "psp-2d api=psp kind=2d state=available" "psp-3d api=psp kind=3d state=available" "ps2-2d api=ps2 kind=2d state=available" \
         "psx-2d api=psx kind=2d state=disabled reason=Coming in PSX Template 5" "psx-3d api=psx kind=3d state=disabled reason=Coming in PSX Template 4" \
         "ps2-3d api=ps2 kind=3d state=disabled reason=Coming in PS2 Template 4"; do
  grep -q "^template: $l" "$D" || fail "hub-dump lacks: $l" "$D"
done
grep -q "^hub-dump: api=psp template=psp-3d" "$D" || fail "the default pick is not psp-3d" "$D"
grep -q "^hub-api: ps2$" "$D" || fail "ps2 is not selectable" "$D"
grep -q "^hub-template: ps2-3d refused: Coming in PS2 Template 4" "$D" || fail "ps2 3D card was not refused" "$D"
grep -q "^hub-template: ps2-2d$" "$D" || fail "ps2 2D card cannot be picked" "$D"
grep -q "^hub-api: psx is not selectable" "$D" || fail "psx should not be selectable" "$D"
grep -q "^hub-dump: api=psp template=psp-2d" "$D" || fail "psp 2D card was not picked" "$D"
for f in dialog_psp dialog_ps2 dialog_psp_2d; do [ -s "$WORK/$f.png" ] || fail "no $f.png" "$D"; done

# ---- 2. create + open + build scripts + Play in editor + Stop ----
play_project() {  # play_project <api> <name>
  api=$1; name=$2; P="$WORK/home_$api"; mkdir -p "$P/.config/rensga"
  RENSGA_HOME="$P" xv "$EDITOR" --no-splash --config-dir "$P/.config/rensga" --auto-rebuild --focus Game --quit-after 900 \
    --ui-event hub-new --ui-event "hub-name:$name" --ui-event "hub-api:$api" --ui-event hub-template:2d --ui-event hub-dump --ui-event hub-submit \
    --ui-event waitbuild --ui-event "openscene:Main" --ui-event "select:Player" --ui-event showgame \
    --ui-event playedit --ui-event "playkeys:Right" --ui-event playwait:20 --ui-event "playdump:Player" \
    --ui-event playwait:120 --ui-event "playdump:Player" --ui-event "shot:$WORK/play_$api.png" \
    --ui-event stopedit --ui-event "shot:$WORK/stopped_$api.png" --ui-event quit > "$WORK/play_$api.txt" 2>&1 || fail "$api: editor exit $?" "$WORK/play_$api.txt"
  O="$WORK/play_$api.txt"; PR="$P/.config/rensga/RensgaProjects/$name"   # HubSettings home = the --config-dir
  grep -q "^hub-create: ok template=$api-2d " "$O" || fail "$api: create refused" "$O"
  [ -f "$PR/ProjectSettings/Project.json" ] && [ -f "$PR/Assets/Scenes/Main.scene" ] || fail "$api: project files missing in $PR" "$O"
  [ ! -e "$PR/thumbnail.png" ] || fail "$api: thumbnail.png was copied into the project" "$O"
  grep -q "\"target\": \"$api\"" "$PR/ProjectSettings/Project.json" || fail "$api: Project.json target" "$O"
  grep -q "^play: started" "$O" || fail "$api: play did not start" "$O"
  grep -q "^play-check: .*identical=1 dirty=0 dirty_before=0 undo=0 undo_before=0 selected_same=1" "$O" || fail "$api: Stop did not restore the scene" "$O"
  grep "^playdump:" "$O" > "$WORK/dumps_$api.txt"
  X1=$(sed -n 1p "$WORK/dumps_$api.txt" | sed 's/.*pos=\([^ ]*\).*/\1/'); X2=$(sed -n 2p "$WORK/dumps_$api.txt" | sed 's/.*pos=\([^ ]*\).*/\1/')
  [ -n "$X1" ] && [ "$X1" != "$X2" ] || { cat "$WORK/dumps_$api.txt"; fail "$api: the player did not move during Play ($X1 -> $X2)" "$O"; }
  [ -s "$WORK/play_$api.png" ] && [ -s "$WORK/stopped_$api.png" ] || fail "$api: no play screenshot" "$O"
  # The Game view draws the desktop (host platform) cook, so no texture may be refused by the GL renderer.
  NT=$(grep -c "texture upload: flags" "$O" || true)
  [ "$NT" -eq 0 ] || fail "$api: $NT texture upload(s) refused in the Game view ($(grep -m1 'texture upload: flags' "$O" | sed 's/.*texture upload: //')): Play in editor shows no sprites or tiles (see play_$api.png)"
  echo "hub_templates_ui: $api-2d created, built, played ($X1 -> $X2)"
}
play_project psp Tpl_Psp2d
play_project ps2 Tpl_Ps22d
echo "hub_templates_ui: ok (screenshots in $WORK)"
