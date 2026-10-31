#!/usr/bin/env bash
# F5.12: RENSGA_REQUIRES_FEATURE. A script that calls a gated API compiles against a generated console_target.gen.h that has the
# feature (psp-3d) and fails with the hint against one that lacks it (ps2-2d has no features), and compiles when no generated
# header is visible at all (engine, editor, tools, tests).
# Usage: feature_gate_check.sh <c++ compiler> <rensga-cooker> <repo root>
set -u
CXX="${1:?compiler}"; COOKER="${2:?cooker}"; ROOT="${3:?repo root}"
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/psp" "$tmp/ps2"
"$COOKER" gen-header --project "$ROOT/templates/psp-3d" --out "$tmp/psp/console_target.gen.h" || exit 1
"$COOKER" gen-header --project "$ROOT/templates/ps2-2d" --out "$tmp/ps2/console_target.gen.h" || exit 1
cat > "$tmp/s.cpp" <<'C'
#include "rensga/Rensga.h"
using namespace rensga;
void script() {
  Application::SetCpuClock(333);
  TouchScreenKeyboard::Open("a");
  (void)SystemInfo::batteryLevel();
  Save::setMode(SaveMode::SystemDialog);
}
C
inc=(-std=c++17 -c -o /dev/null -O0 -I"$ROOT/engine/core/include" -I"$ROOT/engine/api/include")
"$CXX" "${inc[@]}" -I"$tmp/psp" "$tmp/s.cpp" 2>"$tmp/psp.err" || { cat "$tmp/psp.err"; echo "FAIL: psp profile must accept the gated calls"; exit 1; }
"$CXX" "${inc[@]}" "$tmp/s.cpp" 2>"$tmp/none.err" || { cat "$tmp/none.err"; echo "FAIL: no generated header must accept the gated calls"; exit 1; }
if "$CXX" "${inc[@]}" -I"$tmp/ps2" "$tmp/s.cpp" 2>"$tmp/ps2.err"; then echo "FAIL: ps2 profile must reject the gated calls"; exit 1; fi
for hint in "no CPU clock control" "No on-screen keyboard" "no battery information" "no system save dialog"; do
  grep -q "$hint" "$tmp/ps2.err" || { cat "$tmp/ps2.err"; echo "FAIL: missing hint '$hint'"; exit 1; }
done
echo "feature_gate_check: ok"
