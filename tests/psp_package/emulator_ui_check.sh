#!/bin/sh
# F5.04 in the real editor under xvfb, on a fixture catalog (file:// archive, temp RENSGA_HOME, no network):
#   Window > Emulators (real emulator: found, version against the pin) | the emulator is not on PATH: the window says missing, Play on
#   console offers to install it (screenshot), Cancel installs nothing | Install now downloads the fixture, checks sha256, unpacks it |
#   the emulator is found at its pinned version | Reset emulator save area.
# usage: emulator_ui_check.sh <rensga-editor> <project template> <work dir>. Exit 77 = skipped (no xvfb / not Linux x86_64).
EDITOR=$1; TEMPLATE=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
[ "$(uname -sm)" = "Linux x86_64" ] || { echo "skip: fixture catalog is for linux-x86_64"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK/home/.config/rensga" "$WORK/cat" "$WORK/src/emu" "$WORK/nobin"
cp -r "$TEMPLATE" "$WORK/proj" && rm -rf "$WORK/proj/Library"
# the fixture emulator: a script that answers --version (the editor reads it) and nothing else
printf '#!/bin/sh\necho v1.20.4\n' > "$WORK/src/emu/PPSSPPSDL"; chmod +x "$WORK/src/emu/PPSSPPSDL"
dd if=/dev/urandom of="$WORK/src/emu/payload.bin" bs=1024 count=4000 2>/dev/null
tar -czf "$WORK/emu.tar.gz" -C "$WORK/src/emu" .
cat > "$WORK/cat/ppsspp.json" <<JSON
{"id":"ppsspp","name":"PPSSPP (PSP emulator)","kind":"emulator","target":"psp","version":"1.20.4","requires":[],
 "license":"GPL-2.0-or-later","source":"https://example.invalid/ppsspp","notice":"Fixture for the editor test. Not the real emulator.",
 "hosts":{"linux-x86_64":{"status":"available","url":"file://$WORK/emu.tar.gz","sha256":"$(sha256sum "$WORK/emu.tar.gz" | cut -d' ' -f1)","size":$(stat -c %s "$WORK/emu.tar.gz"),"archive":"tar.gz","strip":0},
          "windows-x86_64":{"status":"unpublished","note":"Our CI builds it; not released yet."}},
 "env":{},"path":[],"check":["true"]}
JSON
# a PATH with only what the module installer needs (no emulator in it)
for t in curl wget tar gzip sh true sha256sum; do p="$(command -v $t 2>/dev/null)"; case "$p" in /*) ;; *) p=/usr/bin/$t ;; esac; [ -x "$p" ] && ln -sf "$p" "$WORK/nobin/$t"; done
fail() { echo "FAIL: $1"; tail -40 "$2" 2>/dev/null; exit 1; }
X11="env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s '-screen 0 1600x900x24'"

# 1. the real environment (whatever emulator this computer has, with the real catalog)
eval $X11 "$EDITOR" --project "$WORK/proj" --no-splash --no-auto-rebuild --config-dir "$WORK/home/.config/rensga" --quit-after 120 \
  --ui-event emulators --ui-event emuinfo --ui-event "shot:$WORK/real.png" --ui-event quit > "$WORK/real.txt" 2>&1 || fail "real run" "$WORK/real.txt"
grep -q "^emulators: window open" "$WORK/real.txt" || fail "no window verb output" "$WORK/real.txt"
grep -q "^emulators: target=psp module=ppsspp .* pin=1.20.4 " "$WORK/real.txt" || fail "emuinfo line" "$WORK/real.txt"
grep "^emulators: target" "$WORK/real.txt"

# 2. fixture catalog, no emulator on PATH: missing -> offer -> cancel -> offer -> install -> found
export RENSGA_HOME="$WORK/home" RENSGA_MODULE_CATALOG="$WORK/cat" RENSGA_JOBS=2 RENSGA_EDITOR_TEST_MODULE_DELAY_MS=300 RENSGA_TOOLCHAIN=native
unset RENSGA_PPSSPP RENSGA_PPSSPP_HEADLESS
eval $X11 /usr/bin/env PATH="$WORK/nobin" "$EDITOR" --project "$WORK/proj" --no-splash --no-auto-rebuild --config-dir "$WORK/home/.config/rensga" --quit-after 400 \
  --ui-event emulators --ui-event emuinfo --ui-event "shot:$WORK/missing.png" \
  --ui-event playconsole --ui-event emuinfo --ui-event "shot:$WORK/offer.png" \
  --ui-event emudialogcancel --ui-event emuinfo \
  --ui-event emugate --ui-event emudialoginstall --ui-event "shot:$WORK/offer_progress.png" \
  --ui-event modwait --ui-event emuinfo --ui-event "shot:$WORK/installed.png" \
  --ui-event emureset --ui-event quit > "$WORK/out.txt" 2>&1 || fail "fixture run" "$WORK/out.txt"
O="$WORK/out.txt"
[ "$(grep -c '^emulators: target=psp' "$O")" -ge 4 ] || fail "emuinfo lines" "$O"
grep '^emulators: target=psp' "$O" | sed -n 1p | grep -q "state=missing" || fail "emulator should be missing without PATH" "$O"
grep '^emulators: target=psp' "$O" | sed -n 2p | grep -q "offer=open" || fail "Play on console did not open the install offer" "$O"
grep '^emulators: target=psp' "$O" | sed -n 3p | grep -q "state=missing" || fail "cancel installed something" "$O"
grep '^emulators: target=psp' "$O" | sed -n 4p | grep -q "state=match.*version=1.20.4\|version=1.20.4.*state=match" || fail "emulator not found at the pinned version after the install" "$O"
grep -q "^emugate: psp dialog" "$O" || fail "emugate should open the dialog" "$O"
grep -q "^emureset: ok" "$O" || fail "emureset" "$O"
for f in real missing offer offer_progress installed; do [ -s "$WORK/$f.png" ] || fail "no screenshot $f.png" "$O"; done
echo "emulator manager: missing -> offer -> install -> found 1.20.4; screenshots in $WORK"
