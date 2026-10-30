#!/bin/sh
# F3.04 item 5: a script edited while the scene plays is rebuilt in the background and hot reloaded; the World is rebuilt and
# the running state (a field changed during play, the object's position) survives. Temp copy of templates/psp, xvfb.
# usage: play_hot_reload.sh <rensga-editor> <templates/psp> <work dir>. Exit 77 = skipped.
EDITOR_BIN="$1"; TEMPLATE="$2"; WORK="$3"
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR_BIN" ] || { echo "skip: no editor binary"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK/home"
cp -r "$TEMPLATE" "$WORK/proj" && rm -rf "$WORK/proj/Library"
export RENSGA_HOME="$WORK/home" RENSGA_JOBS=${RENSGA_JOBS:-2}
F=Assets/Scripts/PlayerController.cpp
OUT="$WORK/out.txt"
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR_BIN" --project "$WORK/proj" --no-splash --config-dir "$WORK/home/.config/rensga" --auto-rebuild --quit-after 900 \
  --ui-event waitbuild --ui-event showgame --ui-event playedit \
  --ui-event "playset:Player|2|speed|9" --ui-event playkeys:W --ui-event playwait:30 --ui-event playkeys:none --ui-event pause \
  --ui-event "playdump:Player|2|speed" \
  --ui-event "edit:$F|SERIALIZE_FIELD(Scalar, jumpSpeed, 8.0f)|SERIALIZE_FIELD(Scalar, jumpSpeed, 8.5f)" \
  --ui-event waitbusy --ui-event waitbuild --ui-event step:1 --ui-event "playdump:Player|2|speed" \
  --ui-event "shot:$WORK/after_reload.png" --ui-event stopedit --ui-event quit > "$OUT" 2>&1
RC=$?
fail() { echo "FAIL: $1"; tail -60 "$OUT"; exit 1; }
[ $RC -eq 0 ] || fail "editor exit $RC"
grep -q "scripts reloaded (generation" "$OUT" || fail "no script reload"
grep -q "play: scripts reloaded; running state kept" "$OUT" || fail "the play session did not rebuild its World"
grep "^playdump:" "$OUT" > "$WORK/dumps.txt"
[ "$(wc -l < "$WORK/dumps.txt")" -eq 2 ] || fail "expected two playdump lines"
BEFORE=$(sed -n 1p "$WORK/dumps.txt" | sed 's/.*pos=\([^ ]*\) .*/\1/'); AFTER=$(sed -n 2p "$WORK/dumps.txt" | sed 's/.*pos=\([^ ]*\) .*/\1/')
[ "$BEFORE" != "0.0000,1.0000,0.0000" ] || fail "the script did not move the player before the reload"
[ "$BEFORE" = "$AFTER" ] || fail "position changed across the reload ($BEFORE -> $AFTER)"
grep -q "speed=9" "$WORK/dumps.txt" && [ "$(grep -c 'speed=9' "$WORK/dumps.txt")" -eq 2 ] || fail "the field changed during play did not survive"
grep -q "^play-check: .*identical=1 dirty=0" "$OUT" || fail "stop did not restore the scene"
echo "play_hot_reload: ok ($BEFORE)"
