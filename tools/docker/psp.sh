#!/usr/bin/env bash
# Runs a command inside the pinned pspdev image with the repo (and RENSGA_PROJECT_DIR) mounted at the same paths.
# Usage: tools/docker/psp.sh <command...>     (no args or `true` just checks that docker works)
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
IMAGE_FILE="$HERE/PSPDEV_IMAGE"
IMAGE="pspdev/pspdev"
[ -s "$IMAGE_FILE" ] && IMAGE="$(tr -d '[:space:]' < "$IMAGE_FILE")"

# Re-exec through `sg docker` when this process lacks the docker group (fresh group membership).
if [ -z "${RENSGA_PSP_SG:-}" ] && ! docker info >/dev/null 2>&1; then
  if command -v sg >/dev/null 2>&1 && sg docker -c "docker info" >/dev/null 2>&1; then
    q=""; for a in "$@"; do q+=" $(printf '%q' "$a")"; done
    exec sg docker -c "RENSGA_PSP_SG=1 $(printf '%q' "$0")$q"
  fi
  echo "psp.sh: docker is not usable" >&2; exit 1
fi

if [ "${1:-true}" = "--pin" ]; then
  docker pull pspdev/pspdev:latest >&2 || exit 1
  d="$(docker image inspect --format '{{index .RepoDigests 0}}' pspdev/pspdev:latest)" || exit 1
  echo "$d" > "$IMAGE_FILE"; echo "pinned $d"; exit 0
fi
[ "$#" -eq 0 ] && set -- true
if [ "$1" = "true" ] && [ "$#" -eq 1 ]; then docker info >/dev/null 2>&1; exit $?; fi

if [ ! -s "$IMAGE_FILE" ]; then "$0" --pin || exit 1; IMAGE="$(tr -d '[:space:]' < "$IMAGE_FILE")"; fi
docker image inspect "$IMAGE" >/dev/null 2>&1 || docker pull "$IMAGE" >&2 || exit 1

mounts=(-v "$REPO:$REPO")
for extra in "${RENSGA_PROJECT_DIR:-}" ${RENSGA_DOCKER_MOUNTS:-}; do
  [ -n "$extra" ] && [ -d "$extra" ] && case "$extra/" in "$REPO"/*) ;; *) mounts+=(-v "$extra:$extra");; esac
done
exec docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -e RENSGA_PROJECT_DIR \
  "${mounts[@]}" -w "$PWD" "$IMAGE" "$@"
