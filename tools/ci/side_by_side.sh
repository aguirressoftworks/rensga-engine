#!/usr/bin/env bash
unset WAYLAND_DISPLAY
# F6.05 exercise (Editor 11): the same game built to a console image and to a Windows exe, played side by side (screenshots).
#   PSP      templates/psp-2d, development build through the pipeline (rensga build --platform psp), PPSSPPHeadless, frame dump of the GU
#   Windows  templates/psp-2d, rensga build --platform windows, the exe under wine + Xvfb (software GL), --screenshot
#   PS2      templates/ps2-2d (one project = one profile), rensga build --platform ps2, rensga.iso in PCSX2 (LOCAL ONLY, your BIOS), --shot
# Output: $RENSGA_CI_OUT/side-by-side/{psp,windows,ps2}.png and side-by-side.png (the three next to each other). Exit 77 without the tools.
#   Env: RENSGA_CLI (default build/host/tools/cli/rensga), RENSGA_CI_OUT.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
CLI="${RENSGA_CLI:-$REPO/build/host/tools/cli/rensga}"
[ -x "$CLI" ] || { echo "skip: no rensga CLI at $CLI"; exit 77; }
command -v convert >/dev/null && command -v xvfb-run >/dev/null || { echo "skip: ImageMagick convert or xvfb-run missing"; exit 77; }
OUT="$CI_OUT/side-by-side"; rm -rf "$OUT"; mkdir -p "$OUT"
export RENSGA_TOOLCHAIN="${RENSGA_TOOLCHAIN:-native}" RENSGA_JOBS="${RENSGA_JOBS:-2}"
PARTS=()
fresh() { rm -rf "$2"; cp -r "$1" "$2"; rm -rf "$2/Library" "$2/Builds"; }
# ---- PSP: dev ELF + GU frame dump
if "$CLI" modules path psp-build >/dev/null 2>&1; then
  fresh "$REPO/templates/psp-2d" "$OUT/psp-proj"
  RENSGA_PSP_CMAKE_ARGS="-DRENSGA_GU_DUMP_FRAME=240" RENSGA_PSP_EXIT_AFTER_FRAMES=300 \
    "$CLI" build --platform psp --development --run --headless "$OUT/psp-proj" > "$OUT/psp.log" 2>&1 || { tail -20 "$OUT/psp.log"; exit 1; }
  BMP="$(find "$OUT/psp-proj/Library/Build" -name rensga_shot.bmp | head -1)"
  [ -n "$BMP" ] && python3 -I tools/ci/bmp2png.py "$BMP" "$OUT/psp.png" && PARTS+=("psp:$OUT/psp.png") || echo "side_by_side: no PSP frame dump" >&2
fi
# ---- Windows: the packaged folder under wine
if "$CLI" modules path windows-build >/dev/null 2>&1 && command -v wine >/dev/null; then
  fresh "$REPO/templates/psp-2d" "$OUT/win-proj"
  "$CLI" build --platform windows --development "$OUT/win-proj" > "$OUT/windows-build.log" 2>&1 || { tail -20 "$OUT/windows-build.log"; exit 1; }
  W="$OUT/win-proj/Builds/windows"; EXE="$(ls "$W"/*.exe | head -1)"
  wine_env
  ( cd "$W" && nolockfd env -u WAYLAND_DISPLAY GALLIUM_DRIVER=softpipe LIBGL_ALWAYS_SOFTWARE=1 timeout -s KILL 300 xvfb-run -a -s "-screen 0 1280x720x24" \
      wine "$(basename "$EXE")" --frames 120 --screenshot "Z:$OUT/windows.png" > "$OUT/windows.log" 2>&1 ) || true
  wine_stop
  [ -s "$OUT/windows.png" ] && PARTS+=("windows:$OUT/windows.png") || echo "side_by_side: no Windows screenshot" >&2
fi
# ---- PS2: the ISO of the ps2 sibling project in PCSX2 (needs a BIOS)
if "$CLI" modules path ps2-build >/dev/null 2>&1 && [ -f "$HOME/.config/rensga/emulators.json" ]; then
  fresh "$REPO/templates/ps2-2d" "$OUT/ps2-proj"
  "$CLI" build --platform ps2 --development "$OUT/ps2-proj" > "$OUT/ps2-build.log" 2>&1 || { tail -20 "$OUT/ps2-build.log"; exit 1; }
  RENSGA_PCSX2_WORK="$OUT/pcsx2" "$REPO/tools/emulators/pcsx2_boot.sh" "$OUT/ps2-proj/Builds/ps2/rensga.iso" --frames 240 --log "$OUT/ps2.log" --shot "$OUT/ps2.png" > "$OUT/ps2-boot.log" 2>&1 || true
  [ -s "$OUT/ps2.png" ] && PARTS+=("ps2:$OUT/ps2.png") || echo "side_by_side: no PS2 screenshot (see $OUT/ps2-boot.log)" >&2
fi
[ ${#PARTS[@]} -gt 0 ] || { echo "side_by_side: nothing was captured"; exit 77; }
ARGS=()
for p in "${PARTS[@]}"; do
  n="${p%%:*}"; f="${p#*:}"
  convert "$f" -resize x360 -background '#1b1812' -fill '#f4e9d0' -gravity north -splice 0x28 -pointsize 18 -annotate +0+4 "$n" "$OUT/label-$n.png"
  ARGS+=("$OUT/label-$n.png")
done
convert "${ARGS[@]}" -background '#1b1812' +append "$OUT/side-by-side.png"
echo "side_by_side: ${#PARTS[@]} platforms -> $OUT/side-by-side.png"
