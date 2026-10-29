# Shared by tools/docker/{ps2,windows,android}.sh. Caller sets NAME (ps2|windows|android) and sources this file.
# The base image digest lives in tools/docker/<NAME upper>_IMAGE; the toolchain image is built from
# tools/docker/<NAME>/Dockerfile (all downloads there are version + sha256 pinned) and tagged rensga/<NAME>:<dockerfile hash>.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
UPNAME="$(echo "$NAME" | tr a-z A-Z)"
BASE="$(tr -d '[:space:]' < "$HERE/${UPNAME}_IMAGE")"
TAG="rensga/$NAME:$(sha256sum "$HERE/$NAME/Dockerfile" "$HERE/${UPNAME}_IMAGE" | sha256sum | cut -c1-12)"

if [ -z "${RENSGA_DOCKER_SG:-}" ] && ! docker info >/dev/null 2>&1; then
  if command -v sg >/dev/null 2>&1 && sg docker -c "docker info" >/dev/null 2>&1; then
    q=""; for a in "$@"; do q+=" $(printf '%q' "$a")"; done
    exec sg docker -c "RENSGA_DOCKER_SG=1 $(printf '%q' "${RENSGA_WRAPPER:-$0}")$q"
  fi
  echo "$NAME.sh: docker is not usable" >&2; exit 1
fi
[ "$#" -eq 0 ] && set -- true
if [ "$1" = "true" ] && [ "$#" -eq 1 ]; then docker info >/dev/null 2>&1; exit $?; fi
if ! docker image inspect "$TAG" >/dev/null 2>&1; then
  echo "building toolchain image $TAG (one time)" >&2
  docker build -q --build-arg "BASE=$BASE" -t "$TAG" "$HERE/$NAME" >&2 || exit 1
  docker image prune -f >/dev/null 2>&1   # dangling layers (disk is tight)
fi
mounts=(-v "$REPO:$REPO")
for extra in "${RENSGA_PROJECT_DIR:-}" ${RENSGA_DOCKER_MOUNTS:-}; do
  [ -n "$extra" ] && [ -d "$extra" ] && case "$extra/" in "$REPO"/*) ;; *) mounts+=(-v "$extra:$extra");; esac
done
exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e RENSGA_PROJECT_DIR "${RENSGA_DOCKER_ENV[@]}" \
  "${mounts[@]}" -w "$PWD" "$TAG" "$@"
