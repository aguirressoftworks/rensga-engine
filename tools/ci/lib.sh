# Shared helpers for tools/ci/*.sh (sourced). All build output goes to build/ci/<job>.
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CI_OUT="${RENSGA_CI_OUT:-$REPO/build/ci}"
mkdir -p "$CI_OUT"
JOBS="${RENSGA_JOBS:-4}"
# Reflection codegen on the host for builds inside toolchain images (no Python there): reflect_prescan <dir> [project]
reflect_prescan() { "$REPO/tools/reflect/prescan.sh" "$@"; }
step() { echo "== $*"; }

# F4.01: every cross-target job builds and cooks the 2D slice (samples/platformer2d), not the 3D template.
SLICE_PROJ="$REPO/samples/platformer2d"
SLICE_TOOLS="$CI_OUT/linux-slice"   # host tree with the slice scripts compiled into rensga-cooker and rensga_desktop (no tests, no editor)
# slice_tools [target...]: configure + build the host tree for the slice (default target: rensga-cooker). Use the system cmake: call this
# BEFORE sourcing tools/modules/toolchain_env.sh (a module's PATH would put its cmake first).
slice_tools() {
  [ $# -gt 0 ] || set -- rensga-cooker
  local t; t=(); for x in "$@"; do t+=(--target "$x"); done
  cmake --preset linux-host -B "$SLICE_TOOLS" -DRENSGA_PROJECT_DIR="$SLICE_PROJ" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
  cmake --build "$SLICE_TOOLS" -j"$JOBS" "${t[@]}"
}
# slice_cook <ps2|psp>: cook the slice for that profile (the pack is the same for Windows, Linux and Android: they use the ps2 pack).
# Cooks of a shared project are serialised with the lock the other F4 agents use.
slice_cook() {
  slice_tools rensga-cooker
  if command -v flock >/dev/null; then
    flock /tmp/rensga-cook-platformer2d.lock "$SLICE_TOOLS/tools/cooker/rensga-cooker" cook --project "$SLICE_PROJ" --target "${1:-ps2}"
  else
    "$SLICE_TOOLS/tools/cooker/rensga-cooker" cook --project "$SLICE_PROJ" --target "${1:-ps2}"
  fi
  [ -f "$SLICE_PROJ/Library/Cooked/${1:-ps2}/game.rpak" ] || { echo "slice_cook: no game.rpak was written" >&2; return 1; }
}
SLICE_PACK() { echo "$SLICE_PROJ/Library/Cooked/${1:-ps2}/game.rpak"; }
# slice_pack_check <game.rpak>: refuse a pack the GS/GL renderers cannot upload (PSP-swizzled textures, PSP compact meshes, a PSP
# CTGT profile) so a build-only job cannot ship a game that draws nothing. Needs python3 (no deps).
slice_pack_check() {
  python3 -I "$REPO/tools/ci/pack_check.py" "$@"
}

# Wine hygiene (F6 Stage 4). wine starts services (services.exe, winedevice.exe, ...) that outlive the run and inherit every open fd; one
# that inherited the flock fd of /tmp/rensga-heavy.lock blocked all builds. wine_env sets the prefix and skips the mono / gecko install
# prompts; nolockfd <cmd...> closes every fd >= 3 that points at a /tmp/rensga-*.lock file, then execs; wine_stop kills the prefix's wineserver.
wine_env() {
  export WINEPREFIX="${WINE_PREFIX:-$REPO/build/wine-prefix}" WINEDEBUG=-all WINEARCH=win64 WINEDLLOVERRIDES="mscoree,mshtml="
}
nolockfd() {
  bash -c 'for f in /proc/self/fd/*; do n="${f##*/}"; [ "$n" -ge 3 ] 2>/dev/null || continue; t="$(readlink "$f" 2>/dev/null)"; case "$t" in /tmp/rensga-*.lock*) eval "exec $n>&-" ;; esac; done; exec "$@"' nolockfd "$@"
}
wine_stop() { command -v wineserver >/dev/null && wineserver -k >/dev/null 2>&1 || true; }
