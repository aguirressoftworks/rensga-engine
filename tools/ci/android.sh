#!/usr/bin/env bash
# CI job android: build the debug APK (NDK + SDLActivity + libmain.so) through tools/android (Gradle). Build only.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
reflect_prescan "$REPO/tools/android/build/reflect"
tools/docker/android.sh tools/android/build.sh
mkdir -p "$CI_OUT/android"; cp tools/android/app/build/outputs/apk/debug/app-debug.apk "$CI_OUT/android/rensga-hello.apk"
