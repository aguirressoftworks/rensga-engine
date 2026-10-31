#!/bin/bash
# F5.09 Build and Run in the real editor: the release EBOOT.PBP is built (validate, cook, compile with the psp-build module, package)
# and run in PPSSPPSDL under xvfb; the game must load its scene from the folder of the EBOOT. Run twice on the same project: the second
# (warm) run must be ready in under 30 s. Stop leaves no emulator process behind.
# usage: build_run_ui.sh <rensga-editor> <project template> <work dir>. Exit 77 = no xvfb, PPSSPPSDL or psp-build module.
EDITOR=$1; TEMPLATE=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
command -v PPSSPPSDL >/dev/null 2>&1 || [ -n "${RENSGA_PPSSPP:-}" ] || { echo "skip: no PPSSPPSDL"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
CLI="$(dirname "$EDITOR")/../../tools/cli/rensga"
[ -x "$CLI" ] && "$CLI" modules path psp-build >/dev/null 2>&1 || { echo "skip: psp-build module not installed (rensga modules install psp-build)"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK/home/.config/rensga"
cp -r "$TEMPLATE" "$WORK/proj" && rm -rf "$WORK/proj/Library"
fail() { echo "FAIL: $1"; tail -40 "$2" 2>/dev/null; pkill -f "$WORK/proj" 2>/dev/null; exit 1; }
export RENSGA_TOOLCHAIN=native RENSGA_JOBS=${RENSGA_JOBS:-2}
O="$WORK/out.txt"
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR" --project "$WORK/proj" --no-splash --no-auto-rebuild --config-dir "$WORK/home/.config/rensga" --quit-after 60000 \
  --ui-event emulators --ui-event buildrun --ui-event "buildrunwait:running|900" --ui-event "consolewait:log=rensga: scene loaded|90" \
  --ui-event "shot:$WORK/run1.png" --ui-event buildrunstop --ui-event "buildrunwait:finished|60" \
  --ui-event buildrun --ui-event "buildrunwait:running|300" --ui-event "consolewait:log=rensga: scene loaded|90" \
  --ui-event "shot:$WORK/run2.png" --ui-event buildrunstop --ui-event "buildrunwait:finished|60" --ui-event quit > "$O" 2>&1 || fail "editor exit $?" "$O"
grep "^build-run:" "$O"
[ "$(grep -c '^build-run: wait running done' "$O")" = 2 ] || fail "both runs must reach 'running'" "$O"
grep -q "release EBOOT running" "$O" || fail "no 'ready' line" "$O"
# a booted EBOOT.PBP is mounted as umd0:/ (the dev ELF would say host0:/): the game found its pack next to the EBOOT
grep -q "rensga: data root=umd0:/ (found)" "$O" || fail "the game did not boot from the EBOOT folder (data root)" "$O"
grep -q "rensga: scene loaded" "$O" || fail "the game never loaded a scene" "$O"
grep -q "no usable\|cannot open" "$O" && fail "the EBOOT could not read its pack" "$O"
ms="$(grep '^build-run: wait running done' "$O" | tail -1 | sed -n 's/.*ready_ms=\([0-9]*\).*/\1/p')"
[ -n "$ms" ] && [ "$ms" -lt 30000 ] || fail "warm Build and Run took ${ms:-?} ms (limit 30000)" "$O"
[ -s "$WORK/proj/Library/Build/psp-release/EBOOT.PBP" ] || fail "no release EBOOT.PBP" "$O"
pgrep -f "PPSSPP.*$WORK/proj" >/dev/null && fail "an emulator process is left" "$O"
echo "build and run: warm run ready in ${ms} ms (< 30000); screenshots $WORK/run1.png run2.png"
