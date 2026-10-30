#!/usr/bin/env bash
# F2.G1 (Android): installs the debug APK on an attached phone (adb from build/android-sdk/platform-tools, else PATH), launches
# it and waits for "rensga: alloc-check frames=<N> allocs=0" in logcat. No emulator is downloaded or started.
# Usage: tools/android/run_device_check.sh [apk]   (default build/ci/android/rensga-hello.apk)
# Prints "android: built, not run (no device)" and exits 0 when no device is attached (or the APK is missing: then exits 77).
# Exit: 0 passed or not-run, 1 device present but the check failed, 77 no APK.
set -u
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
APK="${1:-$REPO/build/ci/android/rensga-hello.apk}"
PKG="com.rensga.hello"; TMO="${ANDROID_CHECK_TIMEOUT:-60}"
ADB="$REPO/build/android-sdk/platform-tools/adb"; [ -x "$ADB" ] || ADB="$(command -v adb || true)"
[ -f "$APK" ] || { echo "android: no APK at $APK (run tools/ci/android.sh)"; exit 77; }
if [ -z "$ADB" ]; then echo "android: built, not run (no device)"; exit 0; fi
dev="$("$ADB" devices | awk 'NR>1 && $2=="device"{print $1; exit}')"
if [ -z "$dev" ]; then echo "android: built, not run (no device)"; exit 0; fi
A=("$ADB" -s "$dev")
echo "android: device $dev"
"${A[@]}" install -r "$APK" >/dev/null || { echo "android: install failed" >&2; exit 1; }
"${A[@]}" shell am force-stop "$PKG"; "${A[@]}" logcat -c
"${A[@]}" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
end=$((SECONDS + TMO)); line=""
while [ $SECONDS -lt $end ]; do
  line="$("${A[@]}" logcat -d 2>/dev/null | grep -E 'rensga: alloc-check frames=[0-9]+ allocs=[0-9]+' | tail -1)"
  [ -n "$line" ] && break
  sleep 2
done
"${A[@]}" shell am force-stop "$PKG"
echo "android: ${line:-<no alloc-check line within ${TMO}s>}"
echo "$line" | grep -qE 'frames=[1-9][0-9]* allocs=0' || exit 1
