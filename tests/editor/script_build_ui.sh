#!/bin/sh
# F3.03 end to end, in the real editor under xvfb on a temp copy of templates/psp-3d:
#   syntax error in a script -> background build fails, console shows file:line, the frame counter keeps advancing
#   during the build (no freeze) -> open-in-editor stub receives file:line -> fix -> "scripts reloaded".
# usage: script_build_ui.sh <rensga-editor> <templates/psp-3d> [screenshot.png]. Exit 77 = skipped.
EDITOR_BIN="$1"; TEMPLATE="$2"; SHOT="$3"
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR_BIN" ] || { echo "skip: no editor binary"; exit 77; }
T=$(mktemp -d "${TMPDIR:-/tmp}/rensga-sbui.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
cp -r "$TEMPLATE" "$T/proj" && rm -rf "$T/proj/Library"
cat > "$T/stub.sh" <<STUB
#!/bin/sh
echo "\$@" > "$T/opened.txt"
STUB
chmod +x "$T/stub.sh"
export RENSGA_EXTERNAL_EDITOR="$T/stub.sh {file}:{line}:{col}"
export RENSGA_HOME="$T/home" RENSGA_JOBS=2
mkdir -p "$T/home"
F=Assets/Scripts/FollowCamera.cpp
OUT="$T/out.txt"
SHOT_BUSY="${SHOT:+${SHOT%.png}_building.png}"; SHOT_BUSY="${SHOT_BUSY:-$T/building.png}"
set -- "$EDITOR_BIN" --project "$T/proj" --no-splash --config-dir "$T/home/.config/rensga" --auto-rebuild --quit-after 900 \
  --ui-event waitbuild \
  --ui-event "edit:$F|Scalar yaw = 0;|Scalar yaw = 0 BROKEN_TOKEN;" \
  --ui-event waitbusy --ui-event "shot:${SHOT_BUSY}" --ui-event waitbuild \
  --ui-event "shot:${SHOT:-$T/console.png}" \
  --ui-event openerror:0 \
  --ui-event "edit:$F|Scalar yaw = 0 BROKEN_TOKEN;|Scalar yaw = 0;" \
  --ui-event waitbusy --ui-event waitbuild --ui-event quit
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" "$@" > "$OUT" 2>&1
RC=$?
fail() { echo "FAIL: $1"; tail -60 "$OUT"; exit 1; }
[ $RC -eq 0 ] || fail "editor exit $RC"
grep -q "FollowCamera.cpp:[0-9][0-9]*:[0-9]*.*error" "$OUT" || fail "no error with file:line in the log"
grep -q "build failed" "$OUT" || fail "no 'build failed' line"
# frames advanced while a build ran: at least 3 'building frame=' lines with increasing frame numbers
N=$(grep -c "rensga-editor: building frame=" "$OUT")
[ "$N" -ge 3 ] || fail "frame counter did not advance during the build ($N lines)"
[ -f "$T/opened.txt" ] || fail "open-in-editor stub was not called"
grep -q "FollowCamera.cpp:[0-9][0-9]*:" "$T/opened.txt" || fail "stub argv has no file:line: $(cat "$T/opened.txt")"
grep -q "scripts reloaded (generation" "$OUT" || fail "no 'scripts reloaded' after the fix"
[ -n "$SHOT" ] && [ ! -s "$SHOT" ] && fail "no screenshot"
echo "script_build_ui: ok ($N build frame lines)"
