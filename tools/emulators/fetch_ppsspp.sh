#!/usr/bin/env bash
# MVP-05: records the installed PPSSPP (pacman) into third_party/emulators/ppsspp/{VERSION,SOURCE,LICENSE}
# and prints the binary path. PPSSPP is GPL-2.0+: only ever started as a separate process, never linked.
# Usage: tools/emulators/fetch_ppsspp.sh [--print-path]
set -eu
OUT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)/third_party/emulators/ppsspp"
mkdir -p "$OUT"
BIN=""
if command -v pacman >/dev/null 2>&1 && pacman -Q ppsspp >/dev/null 2>&1; then
  LIST="$(pacman -Ql ppsspp | awk '{print $2}' | grep -E '/bin/[^/]+$')"
  # Prefer the windowed SDL build (what the editor launches), then Qt; never the headless test tool.
  BIN="$(echo "$LIST" | grep -E '/PPSSPPSDL$' | head -1)"
  [ -z "$BIN" ] && BIN="$(echo "$LIST" | grep -E '/PPSSPPQt$' | head -1)"
  VER="$(pacman -Q ppsspp | awk '{print $2}')"
fi
[ -z "$BIN" ] && BIN="$(command -v PPSSPPSDL || command -v ppsspp || true)"
[ -z "$BIN" ] && { echo "PPSSPP not found. Install it: sudo pacman -S ppsspp" >&2; exit 1; }
if [ "${1:-}" = "--print-path" ]; then echo "$BIN"; exit 0; fi
echo "${VER:-unknown}" > "$OUT/VERSION"
{ echo "binary: $BIN"; echo "package: pacman ppsspp ${VER:-unknown}"; echo "source: https://github.com/hrydgard/ppsspp"; echo "license: GPL-2.0-or-later (separate process only)"; } > "$OUT/SOURCE"
LIC="$(ls /usr/share/licenses/ppsspp/* 2>/dev/null | head -1 || true)"
if [ -n "$LIC" ]; then cp "$LIC" "$OUT/LICENSE"; else
  echo "PPSSPP is licensed under the GNU General Public License v2.0 or later. See https://github.com/hrydgard/ppsspp/blob/master/LICENSE.TXT" > "$OUT/LICENSE"; fi
echo "$BIN"
