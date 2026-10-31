#!/usr/bin/env bash
unset WAYLAND_DISPLAY
# F5.12 exercise: profile gating of the script API (RENSGA_REQUIRES_FEATURE). Host compiler only, no console toolchain.
# For each profile (psp, ps2 and psx from the templates, plus psp with "features": []) the cooker writes console_target.gen.h; then
#   - a script that calls every gated API must fail to compile on a profile that lacks the feature, and every error must carry the
#     hint that names the fix (the errors are printed);
#   - the same script must compile on a profile that has all the features (psp);
#   - a script that uses only calls every profile has (the PS2 sample style: Debug, Input, Transform) compiles everywhere.
# Env: RENSGA_COOKER=<path to rensga-cooker> (default: host build in $CI_OUT/linux-host, built if missing), CXX (default c++).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
CXX="${CXX:-c++}"
COOKER="${RENSGA_COOKER:-}"
if [ -z "$COOKER" ]; then
  step "host cooker"
  cmake --preset linux-host -B "$CI_OUT/linux-host" >/dev/null
  cmake --build "$CI_OUT/linux-host" -j"$JOBS" --target rensga-cooker
  COOKER="$CI_OUT/linux-host/tools/cooker/rensga-cooker"
fi
W="$CI_OUT/api-profile"; rm -rf "$W"; mkdir -p "$W"

# ---- one generated header per profile
gen() {  # gen <name> <template> <profile|-> <features json|->
  local name="$1" tpl="$2" prof="$3" feat="$4"
  mkdir -p "$W/$name/proj/ProjectSettings" "$W/$name/inc"
  python3 -I - "$REPO/$tpl/ProjectSettings/ConsoleTarget.json" "$W/$name/proj/ProjectSettings/ConsoleTarget.json" "$prof" "$feat" <<'PY'
import json, sys
src, dst, prof, feat = sys.argv[1:5]
j = json.load(open(src))
if prof != "-":
    j["profile"] = prof
    if prof == "psx":
        j["scalar"] = "fixed"; j["resolution"] = [320, 240]
        j["budgets"]["ramBytes"] = 2097152; j["budgets"]["vramBytes"] = 1048576
if feat != "-": j["features"] = json.loads(feat)
json.dump(j, open(dst, "w"), indent=1)
PY
  "$COOKER" gen-header --project "$W/$name/proj" --out "$W/$name/inc/console_target.gen.h" >/dev/null
}
gen psp templates/psp-3d - -
gen ps2 templates/ps2-2d - -
gen psx templates/ps2-2d psx -
gen psp-bare templates/psp-3d - '[]'

# ---- scripts
cat > "$W/gated.cpp" <<'C'
// Written the way a PS2 sample is, but it calls APIs that need profile features.
#include "rensga/Rensga.h"
using namespace rensga;
// A free function with external linkage: the compiler must emit it, and an `error` attribute only fires on code it emits.
void gatedUpdate(Animation* anim) {
  Application::SetCpuClock(333);                 // cpuClock
  TouchScreenKeyboard::Open("name");             // osk
  (void)SystemInfo::batteryLevel();              // battery
  Save::setMode(SaveMode::SystemDialog);         // saveDialog
  if (anim) anim->Play();                        // skinning
}
C
cat > "$W/common.cpp" <<'C'
// Only calls every profile has: the PS2 sample style.
#include "rensga/Rensga.h"
using namespace rensga;
struct Common : Behaviour {
  void Update() { Debug::Log("tick"); }
};
C
INC=(-std=c++17 -c -o /dev/null -O0 -I"$REPO/engine/core/include" -I"$REPO/engine/api/include")

fail=0
# expect <profile> <script> <ok|fail> [hint ...]
expect() {
  local prof="$1" script="$2" want="$3"; shift 3
  local out="$W/$prof-$script.err"
  step "$script.cpp on $prof (expect $want)"
  if "$CXX" "${INC[@]}" -I"$W/$prof/inc" "$W/$script.cpp" 2>"$out"; then got=ok; else got=fail; fi
  if [ "$got" != "$want" ]; then cat "$out"; echo "FAIL: $script on $prof: expected $want, got $got"; fail=1; return; fi
  if [ "$want" = fail ]; then
    grep -E "error:" "$out" | sed -E 's/^.*(error: )/  \1/' | sort -u
    for h in "$@"; do
      grep -qF "$h" "$out" || { echo "FAIL: no error carries the hint '$h'"; fail=1; }
    done
    # every error line must be one of our hints (no stray compile errors)
    local stray; stray=$(grep -E "error:" "$out" | grep -vF -e "declared with attribute error" || true)
    [ -z "$stray" ] || { echo "FAIL: errors that are not feature hints:"; echo "$stray"; fail=1; }
  fi
}
H_CPU="no CPU clock control"; H_OSK="No on-screen keyboard"; H_BAT="no battery information"; H_SAVE="no system save dialog"; H_SKIN="Skeletal animation is not available"
expect psp      gated  ok
expect ps2      gated  fail "$H_CPU" "$H_OSK" "$H_BAT" "$H_SAVE"
expect psx      gated  fail "$H_CPU" "$H_OSK" "$H_BAT" "$H_SAVE" "$H_SKIN"
expect psp-bare gated  fail "$H_CPU" "$H_OSK" "$H_BAT" "$H_SAVE" "$H_SKIN"
for p in psp ps2 psx psp-bare; do expect $p common ok; done
# ps2 has skinning (CPU skinning per the design doc), so it must not complain about the animation call
grep -qF "$H_SKIN" "$W/ps2-gated.err" && { echo "FAIL: ps2 must accept Animation::Play"; fail=1; }
[ $fail -eq 0 ] && echo "api_profile: ok" || echo "api_profile: FAILED"
exit $fail
