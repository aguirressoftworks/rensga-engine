#!/usr/bin/env bash
# CI only (Ubuntu has no PPSSPPHeadless package): builds PPSSPPHeadless from the pinned tag into $HOME/.local/bin.
# GPL-2.0+: only ever started as a separate process, never linked. Cached by the workflow (key = tag).
set -eu
TAG="${PPSSPP_TAG:-v1.17.1}"
command -v PPSSPPHeadless >/dev/null && exit 0
[ -x "$HOME/.local/bin/PPSSPPHeadless" ] && exit 0
W="$(mktemp -d)"
git clone --depth 1 --branch "$TAG" --recurse-submodules --shallow-submodules https://github.com/hrydgard/ppsspp.git "$W/ppsspp"
cmake -S "$W/ppsspp" -B "$W/b" -DCMAKE_BUILD_TYPE=Release -DHEADLESS=ON -DUSING_QT_UI=OFF -DUSE_WAYLAND_WSI=OFF >/dev/null
cmake --build "$W/b" -j"$(nproc)" --target PPSSPPHeadless
mkdir -p "$HOME/.local/bin"; cp "$W/b/PPSSPPHeadless" "$HOME/.local/bin/"
cp -r "$W/ppsspp/assets" "$HOME/.local/bin/assets" 2>/dev/null || true
