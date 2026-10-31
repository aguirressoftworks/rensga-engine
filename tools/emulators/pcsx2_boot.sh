#!/usr/bin/env bash
# Boots a PS2 ELF or ISO in PCSX2 (separate GPL process, local use only: PS2 cannot boot in public CI) and waits for
# "rensga: frame" heartbeats in the captured EE/system console log.
# Usage: tools/emulators/pcsx2_boot.sh <elf-or-iso> [--frames N] [--timeout S] [--log out.txt]
#   --frames N   success when a heartbeat with frame >= N is seen (default 60, i.e. the first heartbeat)
#   --timeout S  give up after S seconds (default 90)
#   --log FILE   keep the captured console log (default build/pcsx2/last.log; give an absolute path)
#   --sw         use PCSX2's software renderer (Renderer = 13, closest to a real GS); same as env PCSX2_RENDERER=software
#   --shot FILE  also save a PNG of the emulator window once the success heartbeat is seen (ImageMagick `import`)
# Env: RENSGA_PCSX2_WORK=<dir>  sandbox + display folder (default build/pcsx2); give each parallel run its own, they cannot share one.
# PCSX2 comes from the pcsx2 module (tools/emulators/pcsx2_bin.sh), $RENSGA_PCSX2, or PATH.
# Exit: 0 heartbeat seen, 1 timeout/no heartbeat, 2 setup error.
# How it works:
#   * WAYLAND_DISPLAY is unset and Qt/SDL are forced to X11 (xcb), otherwise Qt picks Wayland and opens a real window.
#   * Always runs under its own Xvfb (never opens a window on the desktop), with QT_QPA_PLATFORM=xcb inside that server.
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
RENDERER=12; [ "${PCSX2_RENDERER:-}" = software ] && RENDERER=13
IMG=""; FRAMES=60; TMO=90; OUTLOG=""; SHOT=""
while [ $# -gt 0 ]; do
  case "$1" in
    --frames) FRAMES="$2"; shift 2;; --timeout) TMO="$2"; shift 2;; --log) OUTLOG="$2"; shift 2;; --shot) SHOT="$2"; shift 2;; --sw) RENDERER=13; shift;;
    -h|--help) sed -n 2,13p "$0"; exit 0;; *) IMG="$1"; shift;;
  esac
done
[ -n "$IMG" ] && [ -f "$IMG" ] || { echo "usage: $0 <elf-or-iso> [--frames N] [--timeout S] [--log out.txt]" >&2; exit 2; }
IMG="$(readlink -f "$IMG")"
PCSX2="$("$REPO/tools/emulators/pcsx2_bin.sh")" || { echo "PCSX2 not installed. Run: rensga modules install pcsx2 (or set RENSGA_PCSX2=<path>)" >&2; exit 2; }
export APPIMAGE_EXTRACT_AND_RUN=1   # the portable build is an AppImage; do not require FUSE
command -v Xvfb >/dev/null || { echo "Xvfb not installed" >&2; exit 2; }

CFG="$HOME/.config/rensga/emulators.json"
if [ ! -f "$CFG" ]; then
  mkdir -p "$(dirname "$CFG")"
  printf '{\n  "pcsx2": { "bios": "%s/.config/PCSX2/bios/scph39001.bin" }\n}\n' "$HOME" > "$CFG"
  echo "created $CFG (per-user setting, outside the repo)"
fi
BIOS="$(python3 -I -c 'import json,sys;print(json.load(open(sys.argv[1])).get("pcsx2",{}).get("bios",""))' "$CFG")"
[ -f "$BIOS" ] || { echo "BIOS not found: '$BIOS' (set pcsx2.bios in $CFG)" >&2; exit 2; }
BIOSDIR="$(dirname "$BIOS")"; BIOSFILE="$(basename "$BIOS")"

W="${RENSGA_PCSX2_WORK:-$REPO/build/pcsx2}"; XDG="$W/xdg"; LOG="${OUTLOG:-$W/last.log}"
rm -rf "$XDG"; mkdir -p "$XDG/PCSX2/inis" "$W"
cat > "$XDG/PCSX2/inis/PCSX2.ini" <<INI
[UI]
SettingsVersion = 1
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
Renderer = $RENDERER
[Logging]
EnableSystemConsole = true
EnableEEConsole = true
EnableIOPConsole = true
EnableFileLogging = true
INI
# Renderer 12 = OpenGL through Mesa llvmpipe (LIBGL_ALWAYS_SOFTWARE below): Vulkan/Software renderers fail to create a device
# under Xvfb ("Failed to create render device"), which used to look like a hang with an empty log.
: > "$LOG"
ARGS=(-batch -nogui -fastboot)
case "${IMG,,}" in *.elf) ARGS+=(-elf "$IMG");; *) ARGS+=(-- "$IMG");; esac
HOSTROOT="$(dirname "$IMG")"
# Own Xvfb (not xvfb-run) so the display is known for the screenshot.
rm -f "$W/display"; : > "$W/display"
Xvfb -displayfd 3 -screen 0 1280x720x24 -nolisten tcp 3>"$W/display" >/dev/null 2>&1 &
XVFB=$!
for _ in $(seq 50); do [ -s "$W/display" ] && break; sleep 0.2; done
DSP=":$(tr -d '[:space:]' < "$W/display")"
# Mesa software WSI: Xvfb has no DRI3, so Vulkan presentation (needed even by the Software renderer) only works this way.
( cd "$HOSTROOT" && env -u WAYLAND_DISPLAY MESA_VK_WSI_DEBUG=sw DISPLAY="$DSP" LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe XDG_CONFIG_HOME="$XDG" XDG_DATA_HOME="$XDG/data" XDG_CACHE_HOME="$XDG/cache" QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 \
  timeout "$((TMO + 10))" "$PCSX2" "${ARGS[@]}" >>"$LOG" 2>&1 ) &
RUN=$!
rc=1; end=$((SECONDS + TMO))
while [ $SECONDS -lt $end ] && kill -0 $RUN 2>/dev/null; do
  cat "$XDG/PCSX2/logs/emulog.txt" >>/dev/null 2>&1
  best="$(cat "$LOG" "$XDG"/PCSX2/logs/*.txt 2>/dev/null | grep -a -o 'rensga: frame [0-9]*' | awk '{print $3}' | sort -n | tail -1)"
  if [ -n "$best" ] && [ "$best" -ge "$FRAMES" ]; then rc=0; break; fi
  sleep 1
done
if [ $rc = 0 ] && [ -n "$SHOT" ]; then sleep 2; DISPLAY="$DSP" import -window root "$SHOT" 2>/dev/null && echo "pcsx2_boot: screenshot $SHOT"; fi
cat "$XDG"/PCSX2/logs/*.txt >>"$LOG" 2>/dev/null
# Match the full image path (not just its name): another run may be booting a different "rensga.iso" at the same time.
pkill -f "pcsx2-qt.*${IMG}" 2>/dev/null; sleep 1; pkill -9 -f "pcsx2-qt.*${IMG}" 2>/dev/null
kill $RUN 2>/dev/null; wait $RUN 2>/dev/null; kill $XVFB 2>/dev/null
if [ $rc = 0 ]; then echo "pcsx2_boot: OK, heartbeat frame $best seen (log: $LOG)"; else
  echo "pcsx2_boot: no heartbeat >= $FRAMES within ${TMO}s (log: $LOG)"; grep -a -i "rensga\|error\|bios" "$LOG" | head -10; fi
exit $rc
