#!/usr/bin/env bash
# Boots a PS2 ELF or ISO in PCSX2 (separate GPL process, local use only: PS2 cannot boot in public CI) and waits for
# "rensga: frame" heartbeats in the captured EE/system console log.
# Usage: tools/emulators/pcsx2_boot.sh <elf-or-iso> [--frames N] [--timeout S] [--log out.txt]
#   --frames N   success when a heartbeat with frame >= N is seen (default 60, i.e. the first heartbeat)
#   --timeout S  give up after S seconds (default 90)
#   --log FILE   keep the captured console log (default build/pcsx2/last.log)
# Exit: 0 heartbeat seen, 1 timeout/no heartbeat, 2 setup error.
# How it works:
#   * WAYLAND_DISPLAY is unset and Qt/SDL are forced to X11 (xcb), otherwise Qt picks Wayland and opens a real window.
#   * Always runs under xvfb-run (never opens a window on the desktop), with QT_QPA_PLATFORM=xcb inside that server.
#   * Config is sandboxed: XDG_CONFIG_HOME=build/pcsx2/xdg, so the user's ~/.config/PCSX2 is never read or written.
#   * BIOS: path read from ~/.config/rensga/emulators.json {"pcsx2":{"bios":"<abs path to a BIOS .bin>"}}; created
#     with ~/.config/PCSX2/bios/scph39001.bin if missing. The sandbox only REFERENCES the BIOS directory (Folders/Bios
#     in PCSX2.ini). The BIOS is never copied anywhere.
#   * PCSX2 flags: -batch (exit when the emulation window closes, no prompts) -nogui (no main window) -fastboot
#     (skip the BIOS logo) -elf <file> for ELFs, or the ISO path as the plain argument. Logging comes from the ini:
#     [Logging] EnableSystemConsole/EnableEEConsole/EnableIOPConsole = true, and [EmuCore] HostFs = true
#     (so host: works for dev builds).
#   * The script then polls stdout+stderr of PCSX2 and the sandbox emulog.txt for the heartbeat and kills PCSX2.
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMG=""; FRAMES=60; TMO=90; OUTLOG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --frames) FRAMES="$2"; shift 2;; --timeout) TMO="$2"; shift 2;; --log) OUTLOG="$2"; shift 2;;
    -h|--help) sed -n 2,12p "$0"; exit 0;; *) IMG="$1"; shift;;
  esac
done
[ -n "$IMG" ] && [ -f "$IMG" ] || { echo "usage: $0 <elf-or-iso> [--frames N] [--timeout S] [--log out.txt]" >&2; exit 2; }
IMG="$(readlink -f "$IMG")"
command -v pcsx2-qt >/dev/null || { echo "pcsx2-qt not installed" >&2; exit 2; }
command -v xvfb-run >/dev/null || { echo "xvfb-run not installed" >&2; exit 2; }

CFG="$HOME/.config/rensga/emulators.json"
if [ ! -f "$CFG" ]; then
  mkdir -p "$(dirname "$CFG")"
  printf '{\n  "pcsx2": { "bios": "%s/.config/PCSX2/bios/scph39001.bin" }\n}\n' "$HOME" > "$CFG"
  echo "created $CFG (per-user setting, outside the repo)"
fi
BIOS="$(python3 -I -c 'import json,sys;print(json.load(open(sys.argv[1])).get("pcsx2",{}).get("bios",""))' "$CFG")"
[ -f "$BIOS" ] || { echo "BIOS not found: '$BIOS' (set pcsx2.bios in $CFG)" >&2; exit 2; }
BIOSDIR="$(dirname "$BIOS")"; BIOSFILE="$(basename "$BIOS")"

W="$REPO/build/pcsx2"; XDG="$W/xdg"; LOG="${OUTLOG:-$W/last.log}"
rm -rf "$XDG"; mkdir -p "$XDG/PCSX2/inis" "$W"
cat > "$XDG/PCSX2/inis/PCSX2.ini" <<INI
[UI]
SetupWizardIncomplete = false
ConfirmShutdown = false
StartFullscreen = false
HideMouseCursor = true
[Folders]
UseDefaultBios = false
Bios = $BIOSDIR
[Filenames]
BIOS = $BIOSFILE
[EmuCore]
EnableFastBoot = true
HostFs = true
EnableCheats = false
[EmuCore/GS]
Renderer = 13
[Logging]
EnableSystemConsole = true
EnableEEConsole = true
EnableIOPConsole = true
EnableFileLogging = true
INI
# Renderer 13 = Software; no GPU or GL context needed under Xvfb.
: > "$LOG"
ARGS=(-batch -nogui -fastboot)
case "${IMG,,}" in *.elf) ARGS+=(-elf "$IMG");; *) ARGS+=(-- "$IMG");; esac
HOSTROOT="$(dirname "$IMG")"
( cd "$HOSTROOT" && env -u WAYLAND_DISPLAY XDG_CONFIG_HOME="$XDG" XDG_DATA_HOME="$XDG/data" XDG_CACHE_HOME="$XDG/cache" QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 \
  timeout "$((TMO + 10))" xvfb-run -a -s "-screen 0 1280x720x24" pcsx2-qt "${ARGS[@]}" >>"$LOG" 2>&1 ) &
RUN=$!
rc=1; end=$((SECONDS + TMO))
while [ $SECONDS -lt $end ] && kill -0 $RUN 2>/dev/null; do
  cat "$XDG/PCSX2/logs/emulog.txt" >>/dev/null 2>&1
  best="$(cat "$LOG" "$XDG"/PCSX2/logs/*.txt 2>/dev/null | grep -a -o 'rensga: frame [0-9]*' | awk '{print $3}' | sort -n | tail -1)"
  if [ -n "$best" ] && [ "$best" -ge "$FRAMES" ]; then rc=0; break; fi
  sleep 1
done
cat "$XDG"/PCSX2/logs/*.txt >>"$LOG" 2>/dev/null
pkill -f "pcsx2-qt.*${IMG##*/}" 2>/dev/null; sleep 1; pkill -9 -f "pcsx2-qt.*${IMG##*/}" 2>/dev/null
kill $RUN 2>/dev/null; wait $RUN 2>/dev/null
if [ $rc = 0 ]; then echo "pcsx2_boot: OK, heartbeat frame $best seen (log: $LOG)"; else
  echo "pcsx2_boot: no heartbeat >= $FRAMES within ${TMO}s (log: $LOG)"; grep -a -i "rensga\|error\|bios" "$LOG" | head -10; fi
exit $rc
