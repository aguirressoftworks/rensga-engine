#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# MVP-43: capture a screenshot of the game running in PPSSPP, without a display grab (X grabs of PPSSPP's GL window are black).
# Builds a screenshot ELF (RENSGA_EXIT_AFTER_FRAMES, which dumps the displayed frame to host0:/rensga_shot.bmp on exit),
# runs it in PPSSPPHeadless (software renderer, separate process) and converts the BMP to PNG.
# Usage: tools/emulators/screenshot.sh [project=templates/psp] [out=docs/rensga-engine/mvp/psp-template.png]
# Needs the project cooked first (rensga cook) and Docker (tools/docker/psp.sh).
set -eu
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PROJ="$(cd "${1:-$REPO/templates/psp}" && pwd)"; OUT="${2:-$REPO/docs/rensga-engine/mvp/psp-template.png}"
GEN="$PROJ/Library/Generated/psp"; PACK="$PROJ/Library/Cooked/psp/game.rpak"
S="$REPO/build/shot"; ROOT="$REPO/build/shotroot"
[ -f "$PACK" ] || { echo "cook first: rensga cook $PROJ" >&2; exit 1; }
"$REPO/tools/docker/psp.sh" cmake --preset psp -B "$S" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GEN" -DRENSGA_EXIT_AFTER_FRAMES=240 >/dev/null
"$REPO/tools/docker/psp.sh" cmake --build "$S" --target rensga_game -j4 >/dev/null
rm -rf "$ROOT"; mkdir -p "$ROOT"; cp "$S/engine/runtime/rensga_game.elf" "$PACK" "$ROOT/"
(cd "$ROOT" && timeout 120 xvfb-run -a PPSSPPHeadless "$ROOT/rensga_game.elf" -r "$ROOT" --graphics=software --timeout=90 >/dev/null 2>&1 || true)
[ -f "$ROOT/rensga_shot.bmp" ] || { echo "no screenshot produced" >&2; exit 1; }
mkdir -p "$(dirname "$OUT")"
python3 -I - "$ROOT/rensga_shot.bmp" "$OUT" <<'PY'
import struct, sys, zlib
d = open(sys.argv[1], 'rb').read(); w, h = 480, 272
raw = b''.join(b'\0' + bytes(v for i in range(0, w*3, 3) for v in (r[i+2], r[i+1], r[i]))
               for r in (d[54+(h-1-y)*w*3:54+(h-y)*w*3] for y in range(h)))
c = lambda t, x: struct.pack('>I', len(x)) + t + x + struct.pack('>I', zlib.crc32(t + x))
open(sys.argv[2], 'wb').write(b'\x89PNG\r\n\x1a\n' + c(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + c(b'IDAT', zlib.compress(raw)) + c(b'IEND', b''))
PY
echo "wrote $OUT"
