#!/usr/bin/env bash
# CI job android: build the debug APK (NDK + SDLActivity + libmain.so = the engine player) through tools/android (Gradle). Build only.
# The APK carries the cooked pack of the sample project as assets/game.rpak. Env: RENSGA_ANDROID_PROJECT=<project dir> (default
# samples/platformer2d; its scripts are compiled into the player), RENSGA_ANDROID_PACK=<game.rpak> (default <project>/Library/Cooked/ps2/game.rpak;
# cook it first: build/<x>/tools/cooker/rensga-cooker cook --project samples/platformer2d --target ps2).
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
PROJ="${RENSGA_ANDROID_PROJECT:-$REPO/samples/platformer2d}"; PROJ="$(cd "$PROJ" && pwd)"
PACK="${RENSGA_ANDROID_PACK:-$PROJ/Library/Cooked/ps2/game.rpak}"
if [ ! -f "$PACK" ] && [ -z "${RENSGA_ANDROID_PACK:-}" ]; then
  # Fresh checkout (CI): build the host cooker with the project's scripts and cook the pack for the ps2 profile.
  CB="$CI_OUT/android-cooker"
  cmake --preset linux-host -B "$CB" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
  cmake --build "$CB" -j"$JOBS" --target rensga-cooker
  "$CB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target ps2
fi
[ -f "$PACK" ] || { echo "android: no cooked pack at $PACK (cook the project first, see the header of this script)" >&2; exit 1; }
rm -rf tools/android/build/assets; mkdir -p tools/android/build/assets
cp "$PACK" tools/android/build/assets/game.rpak
[ -f "$(dirname "$PACK")/music.wav" ] && cp "$(dirname "$PACK")/music.wav" tools/android/build/assets/music.wav
reflect_prescan "$REPO/tools/android/build/reflect" "$PROJ"
RENSGA_PROJECT_DIR="$PROJ" tools/docker/android.sh tools/android/build.sh
mkdir -p "$CI_OUT/android"; cp tools/android/app/build/outputs/apk/debug/app-debug.apk "$CI_OUT/android/rensga-player.apk"
unzip -l "$CI_OUT/android/rensga-player.apk" | grep -E 'lib/arm64-v8a/libmain.so|assets/game.rpak'
