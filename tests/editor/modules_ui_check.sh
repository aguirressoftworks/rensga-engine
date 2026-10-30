#!/bin/sh
# Hub > Modules in the real editor under xvfb, on a fixture catalog (file:// archives, temp RENSGA_HOME, no network):
#   Hub modules tab screenshot | Window > Build Support Modules | install with progress | license gate refuses without accept |
#   remove | Play on console with a missing module opens the dialog (cancel installs nothing) | dialog Install now installs the
#   module with its dependency | after it the target is no longer missing.
# usage: modules_ui_check.sh <rensga-editor> <project template> <work dir>. Exit 77 = skipped (no xvfb / not Linux x86_64).
EDITOR=$1; TEMPLATE=$2; WORK=$3
command -v xvfb-run >/dev/null 2>&1 || { echo "skip: no xvfb-run"; exit 77; }
[ -x "$EDITOR" ] || { echo "skip: no editor binary"; exit 77; }
[ "$(uname -sm)" = "Linux x86_64" ] || { echo "skip: fixture catalog is for linux-x86_64"; exit 77; }
rm -rf "$WORK"; mkdir -p "$WORK/home/.config/rensga" "$WORK/cat" "$WORK/src"
cp -r "$TEMPLATE" "$WORK/proj" && rm -rf "$WORK/proj/Library"
mkarc() {  # name: bin/<name> script + payload of $2 KB of random data
  mkdir -p "$WORK/src/$1/bin"
  printf '#!/bin/sh\necho %s\n' "$1" > "$WORK/src/$1/bin/$1"; chmod +x "$WORK/src/$1/bin/$1"
  dd if=/dev/urandom of="$WORK/src/$1/payload.bin" bs=1024 count="$2" 2>/dev/null
  tar -czf "$WORK/$1.tar.gz" -C "$WORK/src/$1" .
}
mkarc fx-tools 64; mkarc fx-build 30000; mkarc fx-sdk 2000
manifest() {  # id name kind target requires extra-json
  f="$WORK/$1.tar.gz"
  cat > "$WORK/cat/$1.json" <<JSON
{"id":"$1","name":"$2","kind":"$3","target":$4,"version":"1.0","requires":$5,
 "license":"MIT","source":"https://example.invalid/$1","notice":"Fixture module for the editor test. Not a real toolchain.",
 "hosts":{"linux-x86_64":{"status":"available","url":"file://$f","sha256":"$(sha256sum "$f" | cut -d' ' -f1)","size":$(stat -c %s "$f"),"archive":"tar.gz","strip":0},
          "windows-x86_64":{"status":"unpublished","note":"Our CI builds it; not released yet."}},
 "env":{},"path":["{root}/bin"],"check":["$1"]$6}
JSON
}
manifest fx-tools "Build Tools" tools null '[]' ''
manifest fx-build "PSP Build Support" toolchain '"psp"' '["fx-tools"]' ''
manifest fx-sdk "Mobile SDK" toolchain null '[]' ',"postInstall":[{"argv":["true"],"license":"fx-sdk-license"}]'
cat > "$WORK/cat/fx-manual.json" <<JSON
{"id":"fx-manual","name":"Emulator (manual)","kind":"emulator","target":null,"version":"1.0","requires":[],"license":"GPL","source":"https://example.invalid/emu","notice":"Run as a separate process.",
 "hosts":{"linux-x86_64":{"status":"manual","note":"Install it from your package manager."},"windows-x86_64":{"status":"manual","note":"Download the portable build."}},"env":{},"path":[],"check":[]}
JSON
export RENSGA_HOME="$WORK/home" RENSGA_MODULE_CATALOG="$WORK/cat" RENSGA_JOBS=2 RENSGA_EDITOR_TEST_MODULE_DELAY_MS=350
fail() { echo "FAIL: $1"; tail -60 "$2" 2>/dev/null; exit 1; }

# 1. Hub (project picker) Modules tab
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR" --no-splash --config-dir "$WORK/home/.config/rensga" --screenshot "$WORK/hub_modules.png" --screenshot-frames 60 \
  --ui-event modhub --ui-event modselect:fx-sdk > "$WORK/hub.txt" 2>&1 || fail "hub run" "$WORK/hub.txt"
[ -s "$WORK/hub_modules.png" ] || fail "no hub screenshot" "$WORK/hub.txt"

# 2. Editor: window, license gate, install with progress, remove, missing-module dialog
env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a -s "-screen 0 1600x900x24" \
  "$EDITOR" --project "$WORK/proj" --no-splash --no-auto-rebuild --config-dir "$WORK/home/.config/rensga" --quit-after 240 \
  --ui-event modules --ui-event modselect:fx-sdk --ui-event "shot:$WORK/window.png" \
  --ui-event modinstall:fx-sdk --ui-event modstate:fx-sdk \
  --ui-event modaccept:fx-sdk --ui-event "shot:$WORK/license.png" --ui-event modinstall:fx-sdk --ui-event "shot:$WORK/progress.png" \
  --ui-event modwait --ui-event modstate:fx-sdk --ui-event "shot:$WORK/installed.png" \
  --ui-event modverify:fx-sdk --ui-event modremove:fx-sdk --ui-event modstate:fx-sdk \
  --ui-event modlocate:fx-manual\|"$WORK" --ui-event modstate:fx-manual \
  --ui-event playconsole --ui-event "shot:$WORK/dialog.png" --ui-event moddialogcancel --ui-event modstate:fx-build \
  --ui-event modgate:psp --ui-event moddialoginstall --ui-event "shot:$WORK/dialog_progress.png" \
  --ui-event modwait --ui-event modstate --ui-event modgate:psp --ui-event "shot:$WORK/final.png" --ui-event quit > "$WORK/out.txt" 2>&1 || fail "editor exit $?" "$WORK/out.txt"
O="$WORK/out.txt"
grep -q "^modinstall: fx-sdk refused: License 'fx-sdk-license' must be accepted" "$O" || fail "license gate did not refuse" "$O"
grep -q "^modstate: id=fx-sdk status=Not installed" "$O" || fail "fx-sdk should still be not installed after the refusal" "$O"
grep -q "^modinstall: fx-sdk started" "$O" || fail "install did not start after accept" "$O"
grep -q "^modstate: id=fx-sdk status=Installed .*root=$WORK/home/modules/fx-sdk-1.0" "$O" || fail "fx-sdk not installed under RENSGA_HOME" "$O"
grep -q "^modverify: fx-sdk ok" "$O" || fail "verify failed" "$O"
grep -q "^modremove: fx-sdk ok" "$O" || fail "remove failed" "$O"
[ ! -d "$WORK/home/modules/fx-sdk-1.0" ] || fail "fx-sdk folder still there after remove" "$O"
grep -q "^modstate: id=fx-manual status=Located" "$O" || fail "manual module not located" "$O"
grep -q "^playconsole: gate=dialog" "$O" || fail "Play on console did not open the missing-module dialog" "$O"
grep -q "^moddialogcancel" "$O" || fail "dialog cancel" "$O"
[ "$(grep -c '^modstate: id=fx-build status=Not installed' "$O")" -ge 1 ] || fail "cancelling the dialog installed something" "$O"
grep -q "^modgate: psp dialog" "$O" || fail "modgate did not open the dialog" "$O"
grep -q "^modstate: id=fx-build status=Installed" "$O" || fail "dialog install did not install PSP Build Support" "$O"
grep -q "^modstate: id=fx-tools status=Installed" "$O" || fail "dependency was not installed" "$O"
grep -q "^modgate: psp proceed" "$O" || fail "after the install the gate should let the build proceed" "$O"
for f in window license progress installed dialog dialog_progress final; do [ -s "$WORK/$f.png" ] || fail "missing screenshot $f" "$O"; done
echo "modules_ui_check: ok"
