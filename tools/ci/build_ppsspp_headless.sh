#!/usr/bin/env bash
# CI only (Ubuntu has no PPSSPPHeadless package): builds PPSSPPHeadless from the pinned tag into $HOME/.local/bin.
# GPL-2.0+: only ever started as a separate process, never linked. Cached by the workflow (key = tag).
# F4.02: ONE PPSSPP version everywhere. The tag below equals "version" in tools/modules/manifest.d/ppsspp.json (the editor's
# emulator module) and the pacman package used on dev machines. A mismatch fails the script.
# The new binary is built in a private directory and swapped in with rename(2), so a job that is running PPSSPPHeadless
# from $HOME/.local/bin never sees a half-written file.
set -eu
TAG="${PPSSPP_TAG:-v1.20.4}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIN="$(sed -n 's/.*"version": *"\([^"]*\)".*/v\1/p' "$HERE/../modules/manifest.d/ppsspp.json" | head -1)"
if [ -n "$PIN" ] && [ "$TAG" != "$PIN" ] && [ -z "${PPSSPP_TAG:-}" ]; then
  echo "ppsspp pin drift: build_ppsspp_headless.sh builds $TAG but manifest.d/ppsspp.json says $PIN" >&2; exit 1
fi
BIN="$HOME/.local/bin"
# Already installed at this version (the marker is written last)?
if [ -x "$BIN/PPSSPPHeadless" ] && [ "$(cat "$BIN/PPSSPPHeadless.version" 2>/dev/null)" = "$TAG" ]; then
  echo "PPSSPPHeadless $TAG already installed in $BIN"; exit 0
fi
# A system PPSSPPHeadless (pacman) is used as is; dev machines keep their package version, which the manifest tracks.
if [ -z "${PPSSPP_FORCE_BUILD:-}" ] && command -v PPSSPPHeadless >/dev/null && [ ! -e "$BIN/PPSSPPHeadless" ]; then exit 0; fi
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
git clone --depth 1 --branch "$TAG" --recurse-submodules --shallow-submodules https://github.com/hrydgard/ppsspp.git "$W/ppsspp"
cmake -S "$W/ppsspp" -B "$W/b" -DCMAKE_BUILD_TYPE=Release -DHEADLESS=ON -DUSING_QT_UI=OFF -DUSE_WAYLAND_WSI=OFF >/dev/null
cmake --build "$W/b" -j"$(nproc)" --target PPSSPPHeadless
mkdir -p "$BIN"
# Stage next to the destination (same filesystem), then rename: assets first, binary, marker last.
S="$BIN/.ppsspp-new.$$"
rm -rf "$S"; mkdir -p "$S"
cp "$W/b/PPSSPPHeadless" "$S/PPSSPPHeadless"
[ -d "$W/ppsspp/assets" ] && cp -r "$W/ppsspp/assets" "$S/assets"
echo "$TAG" > "$S/PPSSPPHeadless.version"
if [ -d "$S/assets" ]; then rm -rf "$BIN/assets.old"; [ -e "$BIN/assets" ] && mv "$BIN/assets" "$BIN/assets.old"; mv "$S/assets" "$BIN/assets"; rm -rf "$BIN/assets.old"; fi
mv -f "$S/PPSSPPHeadless" "$BIN/PPSSPPHeadless"
mv -f "$S/PPSSPPHeadless.version" "$BIN/PPSSPPHeadless.version"
rmdir "$S"
echo "PPSSPPHeadless $TAG installed in $BIN"
