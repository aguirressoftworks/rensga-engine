#!/usr/bin/env bash
# API rules checks (docs/rensga-engine/api-rules.md). Fails on:
#  1. `float`/`double` in game scripts (Assets/Scripts of every template): use Scalar.
#  2. throw/try/catch in the runtime (engine/): no exceptions.
#  3. a public API header without #pragma once, or Rensga.h missing (single umbrella header).
set -u
root="$(cd "$(dirname "$0")/.." && pwd)"
rc=0
strip() { sed -E 's://.*$::'; }

while IFS= read -r f; do
  hits=$(strip < "$f" | grep -nE '\b(float|double)\b' || true)
  if [ -n "$hits" ]; then echo "$f: float/double in a game script (use Scalar):" >&2; echo "$hits" >&2; rc=1; fi
done < <(find "$root/templates" -path '*Assets/Scripts/*' \( -name '*.cpp' -o -name '*.h' \) 2>/dev/null)

while IFS= read -r f; do
  hits=$(strip < "$f" | grep -nE '\b(throw|try|catch)\b' | grep -v '"' || true)
  if [ -n "$hits" ]; then echo "$f: exceptions in the runtime:" >&2; echo "$hits" >&2; rc=1; fi
done < <(find "$root/engine" \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)

[ -f "$root/engine/api/include/rensga/Rensga.h" ] || { echo "missing umbrella header" >&2; rc=1; }
while IFS= read -r f; do
  grep -q '^#pragma once' "$f" || { echo "$f: missing #pragma once" >&2; rc=1; }
done < <(find "$root/engine/api/include" "$root/engine/core/include" -name '*.h')

[ $rc -eq 0 ] && echo "check_api_rules: ok"
exit $rc
