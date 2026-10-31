#!/usr/bin/env bash
# Host-side reflection codegen for cross builds (the toolchain images have no Python).
# Usage: tools/reflect/prescan.sh <reflect-dir> [project-dir]   -> then configure with -DRENSGA_REFLECT_DIR=<reflect-dir>
set -eu
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$(mkdir -p "$1" && cd "$1" && pwd)"
PROJECT="${2:-$REPO/templates/psp-3d}"
exec python3 "$REPO/tools/reflect/reflect_scan.py" --out "$OUT" \
  --group engine "$REPO/engine/core/include/rensga/core/components.h" "$REPO/engine/core/include/rensga/core/gameplay.h" \
  "$REPO/engine/core/include/rensga/core/rigidbody2d.h" \
  --group project "$PROJECT/Assets/Scripts"
