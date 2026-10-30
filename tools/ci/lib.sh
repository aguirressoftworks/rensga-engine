# Shared helpers for tools/ci/*.sh (sourced). All build output goes to build/ci/<job>.
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CI_OUT="${RENSGA_CI_OUT:-$REPO/build/ci}"
mkdir -p "$CI_OUT"
JOBS="${RENSGA_JOBS:-4}"
# Reflection codegen on the host for builds inside toolchain images (no Python there): reflect_prescan <dir> [project]
reflect_prescan() { "$REPO/tools/reflect/prescan.sh" "$@"; }
step() { echo "== $*"; }
