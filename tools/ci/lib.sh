# Shared helpers for tools/ci/*.sh (sourced). All build output goes to build/ci/<job>.
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CI_OUT="$REPO/build/ci"
mkdir -p "$CI_OUT"
JOBS="${RENSGA_JOBS:-4}"
step() { echo "== $*"; }
