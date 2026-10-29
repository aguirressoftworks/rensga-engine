#!/usr/bin/env bash
# Fails if editor/core, tools/pipeline or tools/cli name a console. Comments that cite roadmap
# phases (lines mentioning "F<n>", "Phase", "roadmap") are excluded.
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
pat='psp|psx|ps2|ppsspp|pcsx'
bad=$(grep -rniE "$pat" "$root/editor/core" "$root/tools/pipeline" "$root/tools/cli" \
      --include='*.h' --include='*.cpp' --include='*.txt' --include='*.cmake' --include='*.sh' 2>/dev/null \
      | grep -viE '(//|#).*(\bF[0-9]|phase|roadmap)|rensga_target_' || true)
if [ -n "$bad" ]; then
  echo "console names found in console-agnostic code:" >&2
  echo "$bad" >&2
  exit 1
fi
echo "check_editor_core: ok"
