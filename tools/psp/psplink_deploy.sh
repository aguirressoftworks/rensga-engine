#!/usr/bin/env bash
# F5.03 (hardware preparation, NOT run in CI and not run by the author: there is no PSP here). Deploys a release build to a real PSP
# over USB with psplink: the PSP runs psplink, this host serves the release folder as host0: with usbhostfs_pc and drives the PSP
# with pspsh.
#
#   tools/psp/psplink_deploy.sh [--dry-run] [--no-build] [--serve] [--run-eboot] <project dir>
#
# 1. builds the release package (rensga build --target psp), unless --no-build;
# 2. prints what to do on the PSP (see docs/rensga-engine/psp-hardware-checklist.md, "Before you start");
# 3. starts usbhostfs_pc with <project>/Library/Build/psp-release as host0: (the game finds game.rpak and music*.wav there, because
#    its data probe tries host0:/ after ms0:/PSP/GAME/<folder>/);
# 4. opens pspsh. In it:  cd host0:/   then   ld rensga_game.prx   (the PRX, needs the psplink usermode)  or  ./EBOOT.PBP  (the package).
#    With --run-eboot the script sends "cd host0:/" and "./EBOOT.PBP" for you.
# --dry-run prints every command and starts nothing. The tools come from the PSP Build Support module (rensga modules install psp-build).
# Pause the PSP (HOME button) or Ctrl+C here to stop. Log lines of the game (rensga: ...) show in pspsh because the game logs to stdout.
set -eu
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DRY=0; BUILD=1; SERVE=0; RUNEBOOT=0; PROJ=""
for a in "$@"; do
  case "$a" in
    --dry-run) DRY=1 ;; --no-build) BUILD=0 ;; --serve) SERVE=1 ;; --run-eboot) RUNEBOOT=1 ;;
    -h|--help) sed -n '2,17p' "$0"; exit 0 ;;
    -*) echo "unknown option $a" >&2; exit 2 ;;
    *) PROJ="$a" ;;
  esac
done
[ -n "$PROJ" ] || { echo "usage: $0 [--dry-run] [--no-build] [--serve] [--run-eboot] <project dir>" >&2; exit 2; }
PROJ="$(cd "$PROJ" && pwd)"
CLI="${RENSGA_CLI:-$REPO/build/host/tools/cli/rensga}"
OUT="$PROJ/Library/Build/psp-release"
run() { if [ "$DRY" = 1 ]; then echo "+ $*"; else "$@"; fi; }

if [ "$BUILD" = 1 ]; then
  [ "$DRY" = 1 ] || [ -x "$CLI" ] || { echo "no rensga CLI at $CLI (cmake --build build/host --target rensga, or set RENSGA_CLI)" >&2; exit 1; }
  run "$CLI" build --target psp "$PROJ"
fi
if [ "$DRY" = 0 ]; then
  for f in EBOOT.PBP rensga_game.prx game.rpak; do [ -s "$OUT/$f" ] || { echo "missing $OUT/$f (run without --no-build)" >&2; exit 1; }; done
fi

# Module tools on PATH.
if [ "$DRY" = 0 ]; then
  ENVSH="$("$CLI" modules env psp-build --shell sh 2>/dev/null)" || { echo "PSP Build Support module not installed: rensga modules install psp-build" >&2; exit 1; }
  eval "$ENVSH"
  command -v usbhostfs_pc >/dev/null && command -v pspsh >/dev/null || { echo "usbhostfs_pc / pspsh not found in the module" >&2; exit 1; }
fi

cat <<TXT
== psplink deploy for $PROJ
host0: = $OUT
On the PSP: start psplink (custom firmware, USB cable connected, USBHostFS enabled in psplink.ini: usbhostfs=1).
On Linux, the first connection may need a udev rule for the PSP (vendor 054c) or sudo for usbhostfs_pc.
TXT

if [ "$SERVE" = 1 ] || [ "$RUNEBOOT" = 0 ]; then
  echo "== serving host0: (stop with Ctrl+C); in another terminal:  pspsh   then   cd host0:/  and  ld rensga_game.prx"
  run usbhostfs_pc -b 10000 "$OUT"
  exit 0
fi

# --run-eboot: serve in the background, drive the PSP from pspsh, stop the server on exit.
if [ "$DRY" = 1 ]; then
  echo "+ usbhostfs_pc -b 10000 $OUT &"
  echo "+ pspsh -e 'cd host0:/'  &&  pspsh -e './EBOOT.PBP'"
  exit 0
fi
usbhostfs_pc -b 10000 "$OUT" &
SRV=$!
trap 'kill $SRV 2>/dev/null || true' EXIT INT TERM
sleep 3
pspsh -e 'cd host0:/'
pspsh -e './EBOOT.PBP'
wait $SRV
