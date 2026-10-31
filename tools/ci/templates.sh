#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# F4.09 / F4.10 CI job templates: the 2D Hub templates (templates/psp-2d, templates/ps2-2d), each on a private copy (nothing is written
# into templates/), built the way a user's freshly created project is built.
#   * generator: the committed templates equal tools/templates/make_2d_template.py output (and share their scripts)
#   * linux: both cook for the linux target with a host cooker that has the template scripts built in, and run in rensga_headless
#     (alloc-check allocs=0, a state hash) and in the desktop player under xvfb (screenshot)
#   * psp-2d: cooked for psp + the PSP ELF (psp-build module) booted in PPSSPPHeadless: scene loaded, textures in VRAM, alloc-check
#     allocs=0, frame dump (rensga_shot.bmp) converted to PNG and checked not black
#   * ps2-2d: cooked for ps2, the PS2 ISO built with tools/ci/ps2.sh (ps2-build module); booted in PCSX2 when a BIOS is configured
#     (LOCAL ONLY, tools/emulators/pcsx2_boot.sh), else that last step is skipped and says so
# Flags: --thumbnails   also write templates/<dir>/thumbnail.png (Hub card image) from the desktop screenshots
# Env: RENSGA_TEMPLATES_FRAMES (headless frames, default 400), RENSGA_TEMPLATES_ONLY=psp|ps2|linux (run one part)
# Exit 77 = nothing could run (no cmake/python3), a missing module or emulator skips only its part.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
THUMBS=0; [ "${1:-}" = "--thumbnails" ] && THUMBS=1
ONLY="${RENSGA_TEMPLATES_ONLY:-}"
FRAMES="${RENSGA_TEMPLATES_FRAMES:-400}"
command -v python3 >/dev/null || { echo "templates: python3 missing, skipped"; exit 77; }
W="$CI_OUT/templates"; rm -rf "$W"; mkdir -p "$W"
ran=0; skipped=""
fail() { echo "templates: FAILED: $*" >&2; exit 1; }
want() { [ -z "$ONLY" ] || [ "$ONLY" = "$1" ]; }

step "generator output equals the committed templates"
for p in psp ps2; do python3 -I tools/templates/make_2d_template.py --profile $p --check "templates/$p-2d" || fail "templates/$p-2d is out of date"; done
# private copies without Library/ (a user project starts like this)
for t in psp-2d ps2-2d; do
  mkdir -p "$W/$t"; (cd "templates/$t" && tar --exclude=./Library --exclude=./thumbnail.png -cf - .) | tar -xf - -C "$W/$t"
done
diff -r "$W/psp-2d/Assets/Scripts" "$W/ps2-2d/Assets/Scripts" >/dev/null || fail "the two templates must share their scripts"
tail_log() { tail -${2:-25} "$1" >&2; }

# ---- host tree with the template scripts built in (the scripts of both templates are identical) ----
HT="$CI_OUT/templates-host"   # kept between runs (incremental); the project dir is only used to compile the scripts in
build_host() {
  step "host tools with the template scripts"
  cmake --preset linux-host -B "$HT" -DRENSGA_PROJECT_DIR="$W/psp-2d" -DRENSGA_BUILD_TESTS=OFF -DRENSGA_BUILD_EDITOR=OFF >/dev/null
  cmake --build "$HT" -j"$JOBS" --target rensga-cooker rensga_headless rensga_desktop
}
# system cmake first: the PSP/PS2 module PATH set further down would put cmake 3.28 first, which cannot configure the host tree
build_host
IM=convert; command -v magick >/dev/null && IM=magick   # ImageMagick 7 deprecates `convert`
shot_png() {  # shot_png <in.bmp|png> <out.png> <label>: convert and refuse a black / empty frame
  $IM "$1" "$2" 2>/dev/null || fail "$3: cannot convert $1 (ImageMagick convert missing?)"
  local mean; mean="$($IM "$2" -colorspace Gray -format '%[fx:mean]' info: 2>/dev/null || echo 0)"
  awk -v m="$mean" 'BEGIN { exit !(m > 0.02) }' || fail "$3: the frame is black (mean $mean): $2"
}

# ---- linux: cook + headless + desktop screenshot ----
if want linux; then
  for t in psp-2d ps2-2d; do
    step "$t: cook for linux, run headless"
    flock /tmp/rensga-cook-$t.lock "$HT/tools/cooker/rensga-cooker" cook --project "$W/$t" --target linux --force > "$W/$t.cook.log" 2>&1 || { tail_log "$W/$t.cook.log"; fail "$t: cook for linux"; }
    PACK="$W/$t/Library/Cooked/linux/game.rpak"; [ -s "$PACK" ] || fail "$t: no linux pack"
    "$HT/engine/runtime/rensga_headless" --pack "$PACK" --frames "$FRAMES" --hash > "$W/$t.headless.log" 2>&1 || { tail_log "$W/$t.headless.log"; fail "$t: headless run"; }
    grep -qE "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$W/$t.headless.log" || { tail_log "$W/$t.headless.log"; fail "$t: headless alloc-check is not allocs=0"; }
    grep -E "alloc-check|hash" "$W/$t.headless.log" | tail -2
    step "$t: desktop player screenshot"
    mkdir -p "$W/$t.xdg"
    env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 GALLIUM_DRIVER=softpipe LIBGL_ALWAYS_SOFTWARE=1 XDG_DATA_HOME="$W/$t.xdg" \
      timeout -s KILL 120 xvfb-run -a -s "-screen 0 1280x720x24" "$HT/engine/runtime/rensga_desktop" --pack "$PACK" --frames 90 --screenshot "$W/$t.desktop.png" \
      > "$W/$t.desktop.log" 2>&1 || { tail_log "$W/$t.desktop.log"; fail "$t: desktop player (exit $?; 4 = alloc-check not 0)"; }
    grep -qE "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$W/$t.desktop.log" || { tail_log "$W/$t.desktop.log"; fail "$t: desktop alloc-check is not allocs=0"; }
    shot_png "$W/$t.desktop.png" "$W/$t.desktop.png" "$t desktop"
    echo "$t (linux): ok, $W/$t.desktop.png"
    if [ $THUMBS = 1 ]; then
      case $t in psp-2d) geo=208x117;; *) geo=208x146;; esac
      $IM "$W/$t.desktop.png" -resize "${geo}!" -strip "templates/$t/thumbnail.png"
      echo "$t: wrote templates/$t/thumbnail.png ($geo)"
    fi
  done
  ran=1
fi

# ---- psp-2d in PPSSPPHeadless ----
if want psp; then
  if ! command -v PPSSPPHeadless >/dev/null || ! command -v xvfb-run >/dev/null; then skipped="$skipped psp(no PPSSPPHeadless/xvfb-run)"
  elif ! [ -x "$REPO/tools/modules/toolchain_env.sh" ]; then skipped="$skipped psp(no toolchain_env)"
  else
    P="$W/psp-2d"; B="$W/psp"; ROOT="$W/psp-root"
    # The project-built cooker (the template scripts are compiled in): `rensga cook` only knows the scripts of the project the CLI was built with.
    step "psp-2d: cook for psp"
    flock /tmp/rensga-cook-psp-2d.lock "$HT/tools/cooker/rensga-cooker" cook --project "$P" --target psp --force > "$W/psp-2d.cook-psp.log" 2>&1 || { tail_log "$W/psp-2d.cook-psp.log"; fail "psp-2d: cook for psp"; }
    mkdir -p "$P/Library/Generated/psp"
    "$HT/tools/cooker/rensga-cooker" gen-header --project "$P" --out "$P/Library/Generated/psp/console_target.gen.h" >/dev/null || fail "psp-2d: gen-header"
    tail -2 "$W/psp-2d.cook-psp.log"
    [ -s "$P/Library/Cooked/psp/game.rpak" ] || fail "psp-2d: no psp pack"
    if . "$REPO/tools/modules/toolchain_env.sh" psp-build psp 2>"$W/psp.env.log"; then
      GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
      step "psp-2d: PSP ELF"
      reflect_prescan "$B/reflect" "$P"
      rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$P" -DRENSGA_GENERATED_DIR="$P/Library/Generated/psp" -DRENSGA_EXIT_AFTER_FRAMES="$FRAMES" >/dev/null
      rensga_tc cmake --build "$B" --target rensga_game -j"$JOBS"
      rm -rf "$ROOT"; mkdir -p "$ROOT"; cp "$B/engine/runtime/rensga_game.elf" "$P/Library/Cooked/psp/game.rpak" "$ROOT/"
      cp "$P"/Library/Cooked/psp/*.wav "$ROOT/" 2>/dev/null || true   # streamed music sits next to the pack
      step "psp-2d: boot in PPSSPPHeadless"
      log="$(cd "$ROOT" && timeout -s KILL 150 xvfb-run -a env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 \
              PPSSPPHeadless "$ROOT/rensga_game.elf" -r "$ROOT" --graphics=software --timeout=120 2>&1 || true)"
      echo "$log" | tail -5
      [ -f "$ROOT/rensga_shot.bmp" ] || fail "psp-2d did not reach its exit frame (no rensga_shot.bmp)"
      L="$ROOT/rensga.log"
      grep -q "rensga: scene loaded" "$L" || fail "psp-2d never loaded a scene (see $L)"
      ! grep -qE "no usable|cannot open|upload failed" "$L" || { grep -E "no usable|cannot open|upload failed" "$L" >&2; fail "psp-2d: pack or texture problem (see $L)"; }
      grep -qE "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$L" || fail "psp-2d: alloc-check is not allocs=0 (see $L)"
      grep -hE "scene loaded|vram tex=|alloc-check|mem budget" "$L" | tail -4
      shot_png "$ROOT/rensga_shot.bmp" "$W/psp-2d.ppsspp.png" "psp-2d"
      echo "psp-2d (PPSSPP): ok, $W/psp-2d.ppsspp.png"; ran=1
    else skipped="$skipped psp(PSP Build Support module not installed: rensga modules install psp-build)"; fi
  fi
fi

# ---- ps2-2d: cook for ps2, ISO, PCSX2 when a BIOS is configured ----
if want ps2; then
  if ! [ -x "$REPO/tools/modules/toolchain_env.sh" ] || ! ( . "$REPO/tools/modules/toolchain_env.sh" ps2-build ps2 ) >/dev/null 2>&1; then
    skipped="$skipped ps2(ps2-build module not installed: rensga modules install ps2-build)"
  else
    P="$W/ps2-2d"
    step "ps2-2d: cook for ps2"
    flock /tmp/rensga-cook-ps2-2d.lock "$HT/tools/cooker/rensga-cooker" cook --project "$P" --target ps2 --force > "$W/ps2-2d.cook2.log" 2>&1 || { tail_log "$W/ps2-2d.cook2.log"; fail "ps2-2d: cook for ps2"; }
    step "ps2-2d: PS2 ISO"
    RENSGA_PS2_PROJECT="$P" RENSGA_PS2_PACK="$P/Library/Cooked/ps2/game.rpak" RENSGA_PS2_BUILD="$W/ps2" \
      RENSGA_PS2_CMAKE_ARGS="-DRENSGA_PS2_CHECK_FRAMES=300" "$REPO/tools/ci/ps2.sh" > "$W/ps2.build.log" 2>&1 || { tail_log "$W/ps2.build.log" 30; fail "ps2-2d: ISO build"; }
    [ -s "$W/ps2/rensga.iso" ] || fail "ps2-2d: no ISO"
    ls -l "$W/ps2/rensga.iso" | awk '{print "  " $5 " bytes  " $9}'
    ran=1
    CFGJ="$HOME/.config/rensga/emulators.json"
    if "$REPO/tools/emulators/pcsx2_bin.sh" >/dev/null 2>&1 && { [ -f "$CFGJ" ] || ls "$HOME/.config/PCSX2/bios/"*.bin >/dev/null 2>&1; }; then
      step "ps2-2d: boot in PCSX2 (local)"
      RENSGA_PCSX2_WORK="$W/pcsx2" "$REPO/tools/emulators/pcsx2_boot.sh" "$W/ps2/rensga.iso" --frames 330 --timeout 600 --log "$W/ps2-2d.pcsx2.log" --shot "$W/ps2-2d.pcsx2.png" \
        || fail "ps2-2d: PCSX2 boot (see $W/ps2-2d.pcsx2.log)"
      grep -aqE "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$W/ps2-2d.pcsx2.log" || fail "ps2-2d: alloc-check is not allocs=0 (see $W/ps2-2d.pcsx2.log)"
      ! grep -aqE "upload failed|uploadTexture:|ERROR \[gs\]" "$W/ps2-2d.pcsx2.log" || fail "ps2-2d: a texture upload failed on the GS"
      grep -a "alloc-check" "$W/ps2-2d.pcsx2.log" | tail -1
      [ -s "$W/ps2-2d.pcsx2.png" ] && echo "ps2-2d (PCSX2): ok, $W/ps2-2d.pcsx2.png"
    else
      skipped="$skipped ps2-boot(no PCSX2 or BIOS configured; the ISO was built)"
    fi
  fi
fi

[ -z "$skipped" ] || echo "templates: skipped:$skipped"
[ $ran = 1 ] || { echo "templates: nothing ran"; exit 77; }
echo "templates: ok (output in $W)"
