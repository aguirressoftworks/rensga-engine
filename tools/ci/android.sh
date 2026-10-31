#!/usr/bin/env bash
# CI job android: build the debug APK (NDK + SDLActivity + libmain.so = the engine player) through tools/android (Gradle). Build only.
# Toolchain: the android-build module (native, default; rensga modules install android-build) or Docker (RENSGA_TOOLCHAIN=docker).
# The APK carries the cooked pack of the 2D slice (F4.01) as assets/game.rpak plus every streamed track (assets/music*.wav). This job cooks it
# itself (tools/ci/lib.sh slice_cook). Env: RENSGA_ANDROID_PROJECT=<project dir> (default samples/platformer2d; its scripts are compiled
# into the player), RENSGA_ANDROID_PACK=<game.rpak> (default: the slice cooked here; for another project <project>/Library/Cooked/ps2/game.rpak).
# Exit 77 locally when the android-build module is not installed (it needs Google's SDK license; this script never accepts it).
# The phone run is manual and deferred: tools/ci/android_device.sh prints "built, not run" without a device.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
PROJ="${RENSGA_ANDROID_PROJECT:-$SLICE_PROJ}"; PROJ="$(cd "$PROJ" && pwd)"
# Fail fast, before the (slow) cook: is the toolchain module there? Locally the Android SDK license is the user's to accept, so a missing
# module is a skip (77) and this script never tries to accept it; in CI (which passes --accept-licenses on install) it is a failure.
. "$REPO/tools/modules/toolchain_env.sh"
if ! ( rensga_tc_setup android-build android ) >/dev/null 2>&1; then
  if [ -z "${CI:-}" ]; then
    echo "android: SKIP, the android-build module is not installed. It needs Google's Android SDK license, which only you can accept: run 'rensga modules install android-build' (it asks), then re-run." >&2
    exit 77
  fi
  rensga_tc_setup android-build android; exit 1
fi
if [ -n "${RENSGA_ANDROID_PACK:-}" ]; then PACK="$RENSGA_ANDROID_PACK"
elif [ "$PROJ" = "$SLICE_PROJ" ]; then
  # The slice (F4.01): host cooker with its scripts, ps2 profile pack (640x448; the Android player scales it).
  step "cook the slice"; slice_cook ps2; PACK="$(SLICE_PACK ps2)"
else PACK="$PROJ/Library/Cooked/ps2/game.rpak"; fi
[ -f "$PACK" ] || { echo "android: no cooked pack at $PACK (cook the project first, see the header of this script)" >&2; exit 1; }
slice_pack_check "$PACK" || exit 1
rm -rf tools/android/build/assets; mkdir -p tools/android/build/assets
cp "$PACK" tools/android/build/assets/game.rpak
# Every streamed track next to the pack (music.wav, music2.wav, music3.wav): the player falls back to music.wav for a missing one.
for f in "$(dirname "$PACK")"/*.wav; do [ -f "$f" ] && cp "$f" tools/android/build/assets/; done
reflect_prescan "$REPO/tools/android/build/reflect" "$PROJ"
export GRADLE_USER_HOME="${GRADLE_USER_HOME:-$REPO/build/gradle-cache}"; mkdir -p "$GRADLE_USER_HOME"
rensga_tc_setup android-build android || exit 1
step "android toolchain: $RENSGA_TOOLCHAIN_BACKEND"
RENSGA_PROJECT_DIR="$PROJ" rensga_tc tools/android/build.sh
mkdir -p "$CI_OUT/android"; cp tools/android/app/build/outputs/apk/debug/app-debug.apk "$CI_OUT/android/rensga-player.apk"
unzip -l "$CI_OUT/android/rensga-player.apk" | grep -E 'lib/arm64-v8a/libmain.so|assets/game.rpak|assets/music[0-9]*.wav'
