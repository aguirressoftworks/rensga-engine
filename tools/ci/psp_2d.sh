#!/usr/bin/env bash
unset WAYLAND_DISPLAY  # xvfb-run must never let SDL/Qt reach a real Wayland desktop
# CI job psp_2d (F4.07): the 2D slice on PSP. samples/platformer2d_psp is cooked for psp, built as a PSP ELF with the TitleBot autostart
# (a bot plays the whole game: no pad in an emulator) and booted in PPSSPPHeadless (pinned 1.20.4, software renderer).
#
#   tools/ci/psp_2d.sh [mode...]       default mode: slice
#     slice        game complete, allocs=0, saves written, a frame of the title, each room and the results as PNG
#     gles         the same ELF under --graphics=gles (the GPU renderer): game completes, no "Unknown syscall" or GPU errors (the frame
#                  dump is stale there, see slice())
#     input        F4.03: the InputTest scene (every button, the nub) + the memory-budget log
#     stress       F4.04: 1000 sprites on PSP, and on PS2 in PCSX2 when the ps2-build module and a BIOS exist (table)
#     audio        F4.06: 16 voices + music at 222 and 333 MHz (mix_us table)
#     determinism  tools/ci/determinism.sh 2d (Linux headless vs the PSP sim ELF: equal state hashes)
#     all          every mode above
# Output: $RENSGA_CI_OUT/psp2d/ (logs, shots/*.png, exercises.md). Env: RENSGA_PSP_2D_PROJECT (default samples/platformer2d_psp).
# Exit 77 when the PSP Build Support module or PPSSPPHeadless / xvfb-run is missing.
set -eu
. "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
cd "$REPO"
for t in PPSSPPHeadless xvfb-run python3; do command -v $t >/dev/null || { echo "skip: $t not installed"; exit 77; }; done
HOST_PATH="$PATH"   # the module's cmake (3.28) cannot configure the host tree: host cmake runs with the PATH from before the module env
. "$REPO/tools/modules/toolchain_env.sh" psp-build psp || { echo "skip: PSP Build Support module not installed (rensga modules install psp-build)"; exit 77; }
MODES="${*:-slice}"; [ "$MODES" = all ] && MODES="slice gles input stress audio determinism"
PROJ="$(cd "${RENSGA_PSP_2D_PROJECT:-$REPO/samples/platformer2d_psp}" && pwd)"
OUT="$CI_OUT/psp2d"; SHOTS="$OUT/shots"; ROOT="$OUT/root"; B="$CI_OUT/psp2d-build"; HB="$CI_OUT/linux-host-2d"
mkdir -p "$OUT" "$SHOTS"
GEN=(); [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && GEN=(-G Ninja)
EMU=(env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 xvfb-run -a)
fail() { echo "psp_2d: $*" >&2; exit 1; }

# ---- cook (host cooker with the sample's scripts built in) ------------------------------------------------------------------------
step "host cooker (RENSGA_PROJECT_DIR=$(basename "$PROJ"))"
env PATH="$HOST_PATH" cmake --preset linux-host -B "$HB" -DRENSGA_PROJECT_DIR="$PROJ" >/dev/null
env PATH="$HOST_PATH" cmake --build "$HB" -j"$JOBS" --target rensga-cooker
GENDIR="$PROJ/Library/Generated/psp"
step "cook for psp"
( flock 9; "$HB/tools/cooker/rensga-cooker" cook --project "$PROJ" --target psp --gen-header "$GENDIR/console_target.gen.h" ) 9>"/tmp/rensga-cook-$(basename "$PROJ").lock" | tail -4
PACK="$PROJ/Library/Cooked/psp/game.rpak"
[ -s "$PACK" ] || fail "no $PACK"
ls -l "$PACK" | awk '{print "  pack " $5 " bytes"}'
reflect_prescan "$B/reflect" "$PROJ"

# Every test-only cache entry is reset on each build so an earlier variant in this tree cannot leak into the next one.
psp_build() { # <cmake -D args...>  -> $B/engine/runtime/rensga_game.elf
  rensga_tc cmake --preset psp "${GEN[@]}" -B "$B" -DRENSGA_REFLECT_DIR="$B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" -DRENSGA_GENERATED_DIR="$GENDIR" \
    -DRENSGA_PSP_AUTOSTART_SCENE= -DRENSGA_EXIT_AFTER_FRAMES=0 -DRENSGA_PSP_STRESS_SPRITES=0 -DRENSGA_PSP_AUDIO_STRESS=OFF \
    -DRENSGA_PSP_CPU_MHZ=333 -DRENSGA_GU_DUMP_FRAME=0 -DRENSGA_GU_DUMP_FRAMES= -DRENSGA_GU_SELFTEST=OFF "$@" >/dev/null
  rensga_tc cmake --build "$B" --target rensga_game -j"$JOBS" 2>&1 | tail -2
}
stage_root() { # a clean host0: root with the ELF, the pack and the streamed music
  rm -rf "$ROOT"; mkdir -p "$ROOT"
  cp "$B/engine/runtime/rensga_game.elf" "$PACK" "$ROOT/"
  for f in "$PROJ"/Library/Cooked/psp/*.wav "$PROJ"/Streaming/*.wav; do [ -f "$f" ] && cp -n "$f" "$ROOT/"; done
  true
}
boot() { # <graphics software|gles> <timeout s> <logfile>  : runs the ELF in $ROOT; the game log is host0:/rensga.log
  local g="$1" t="$2" log="$3"
  ( cd "$ROOT" && timeout -s KILL $((t + 90)) "${EMU[@]}" PPSSPPHeadless "$ROOT/rensga_game.elf" -r "$ROOT" -l --graphics="$g" --timeout="$t" > "$log.emu" 2>&1 || true )
  cp "$ROOT/rensga.log" "$log" 2>/dev/null || : > "$log"
}
need() { grep -qE "$1" "$2" || fail "$3 (see $2)"; }
lastprof() { grep -E "rensga: prof" "$1" | tail -1 | sed 's/.*rensga: prof //'; }
field() { echo "$1" | tr ' ' '\n' | sed -n "s/^$2=//p" | head -1; }  # field "<prof line>" render_us -> avg/max
shots() { # <prefix> : convert $ROOT/rensga_shot*.bmp -> $SHOTS/<prefix>_*.png
  local f n
  for f in "$ROOT"/rensga_shot*.bmp; do
    [ -f "$f" ] || continue
    n="$(basename "$f" .bmp)"; n="${n#rensga_shot}"; n="${n#_}"; [ -n "$n" ] || n=end
    python3 -I "$REPO/tools/ci/bmp2png.py" "$f" "$SHOTS/$1_$n.png" || fail "$1: frame $n is a single color (blank)"
  done
}
EXLOG="$OUT/exercises.md"; : > "$EXLOG"

slice() { # sets nothing; used by slice and gles
  local g="$1" tag="$2"
  stage_root
  boot "$g" 420 "$OUT/$tag.log"
  need "rensga: boot" "$OUT/$tag.log" "$tag: the game did not start"
  need "game complete coins=19" "$OUT/$tag.log" "$tag: the bot did not finish the game"
  need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/$tag.log" "$tag: no alloc-check allocs=0"
  need "rensga: quit after" "$OUT/$tag.log" "$tag: the game did not reach its exit frame"
  if grep -q "Unknown syscall" "$OUT/$tag.log.emu"; then fail "$tag: Unknown syscall in the emulator log"; fi
  grep -E "game complete|alloc-check|quit after|room cleared|vram tex|renderer ready|have.freq" "$OUT/$tag.log" | sed 's/^/  /'
  # PPSSPP's GLES backend renders into its own FBOs and does not read them back into the emulated VRAM that GuRenderer::dumpFrame reads:
  # every dump of a gles run is the first frame (stale). So the gles run checks the log, not the pixels; the software run checks the pixels.
  if [ "$g" = software ]; then
    shots "$tag"
    ls "$SHOTS"/"$tag"_*.png | sed 's/^/  shot /'
  else
    n="$(grep -cE "^E .*(GE|GPU|Unsupported|Bad|Invalid)" "$OUT/$tag.log.emu" || true)"
    echo "  gles: frame dump not checked (stale VRAM readback, a PPSSPP limit); GE/GPU error lines in the emulator log: $n"
    [ "$n" = 0 ] || { grep -E "^E .*(GE|GPU|Unsupported|Bad|Invalid)" "$OUT/$tag.log.emu" | head -5 >&2; fail "$tag: GPU errors in the emulator log"; }
  fi
}

if echo " $MODES " | grep -qE " (slice|gles) "; then
  step "psp build: TitleBot, exit after 3500 frames, dumps of the title, rooms 1-3 and the results"
  psp_build -DRENSGA_PSP_AUTOSTART_SCENE=TitleBot -DRENSGA_EXIT_AFTER_FRAMES=3500 -DRENSGA_GU_DUMP_FRAMES="20,400,1000,1800"
  for m in $MODES; do
    case "$m" in
      slice)
        step "slice: PPSSPPHeadless, software renderer"
        slice software psp2d_sw
        # saves: the game writes room/coins/lives/ticks and best_time/best_coins through Save (save:/ = a folder next to the ELF)
        n="$(find "$ROOT" -type f \( -name best_time -o -name 'best_time*' \) | wc -l)"
        [ "$n" -ge 1 ] || fail "slice: no best_time save written under $ROOT"
        find "$ROOT" -type f \( -name room -o -name coins -o -name lives -o -name ticks -o -name 'best_*' \) | sed "s|$ROOT/|  save |"
        echo "psp_2d slice: OK (game complete, allocs=0, saves written)";;
      gles)
        step "gles: PPSSPPHeadless --graphics=gles (the GPU renderer)"
        slice gles psp2d_gles
        echo "psp_2d gles: OK";;
    esac
  done
fi

for m in $MODES; do
  case "$m" in
    input)
      step "input: InputTest scene (F4.03)"
      psp_build -DRENSGA_PSP_AUTOSTART_SCENE=InputTest -DRENSGA_EXIT_AFTER_FRAMES=130 -DRENSGA_GU_DUMP_FRAMES="100"
      stage_root; boot software 60 "$OUT/input.log"
      need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/input.log" "input: no alloc-check allocs=0"
      need "rensga: mem budget=" "$OUT/input.log" "input: no memory budget line"
      grep -E "mem budget|sdl controller|input|InputTest|alloc-check" "$OUT/input.log" | sed 's/^/  /'
      shots psp_input; ls "$SHOTS"/psp_input_*.png | sed 's/^/  shot /'
      { echo "### Input test (PPSSPPHeadless)"; grep -E "mem budget|controller" "$OUT/input.log" | sed 's/^/    /'; echo "The Home button and the real pad are a manual check on hardware."; echo; } >> "$EXLOG";;
    stress)
      # Same test on both consoles (F2.02): no pack, so the runtime draws only the stress sprites (PS2: gameMode off; PSP: built-in cube).
      # A 2D pack would hide them behind its opaque UI, and the PS2 runtime draws no stress in game mode.
      step "stress: 1000 sprites on PSP (F4.04), no pack"
      psp_build -DRENSGA_PSP_STRESS_SPRITES=1000 -DRENSGA_EXIT_AFTER_FRAMES=300 -DRENSGA_GU_DUMP_FRAMES="250"
      stage_root; rm -f "$ROOT/game.rpak" "$ROOT"/*.wav; boot software 60 "$OUT/stress_psp.log"
      need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/stress_psp.log" "stress: PSP alloc-check not 0"
      need "rensga: stress sprites=1000" "$OUT/stress_psp.log" "stress: the PSP build did not draw 1000 sprites"
      shots psp_stress; ls "$SHOTS"/psp_stress_*.png | sed 's/^/  shot /'
      pp="$(lastprof "$OUT/stress_psp.log")"; echo "  psp prof: $pp"
      pa="$(grep -h 'alloc-check' "$OUT/stress_psp.log" | tail -1 | sed 's/.*rensga: //')"
      row_ps2="| PS2 640x448 (PCSX2) | not run | | | |"
      PS2B="$CI_OUT/ps2-stress"
      if [ -f "$HOME/.config/rensga/emulators.json" ] && ( PATH="$HOST_PATH"; . "$REPO/tools/modules/toolchain_env.sh" ps2-build ps2 ) >/dev/null 2>&1; then
        step "stress: 1000 sprites on PS2 in PCSX2 (local only)"
        reflect_prescan "$PS2B/reflect" "$PROJ"
        ( . "$REPO/tools/modules/toolchain_env.sh" ps2-build ps2
          PG=(); BD="$PS2B"; [ "$RENSGA_TOOLCHAIN_BACKEND" = native ] && { PG=(-G Ninja); BD="$PS2B/native"; }
          rensga_tc cmake --preset ps2 "${PG[@]}" -B "$BD" -DRENSGA_REFLECT_DIR="$PS2B/reflect" -DRENSGA_PROJECT_DIR="$PROJ" \
            -DRENSGA_PS2_STRESS_SPRITES=1000 -DRENSGA_PS2_CHECK_FRAMES=300 -DRENSGA_PS2_AUTOSTART= -DRENSGA_PS2_VRAM_STRESS=OFF \
            -DRENSGA_PS2_SAVE_SELFTEST=OFF -DRENSGA_PS2_FORMAT_BLANK_CARD=OFF -DRENSGA_PS2_AUDIO_STRESS=0 -DRENSGA_PS2_SHOT_FRAME=0 >/dev/null
          rensga_tc cmake --build "$BD" --target rensga_ps2 -j"$JOBS" 2>&1 | tail -1
          "$REPO/tools/ps2/make_iso.sh" "$BD/engine/runtime/rensga_ps2.elf" "$PS2B/rensga.iso" ) > "$OUT/stress_ps2_build.log" 2>&1 \
          || { tail -5 "$OUT/stress_ps2_build.log"; fail "stress: the PS2 build failed"; }
        ( flock 8; "$REPO/tools/emulators/pcsx2_boot.sh" "$PS2B/rensga.iso" --frames 360 --timeout 150 --log "$OUT/stress_ps2.log" ) 8>/tmp/rensga-pcsx2.lock \
          || echo "  (pcsx2_boot.sh exited non-zero; the log is still read)"
        qa="$(lastprof "$OUT/stress_ps2.log")"; echo "  ps2 prof: $qa"
        qaa="$(grep -h 'alloc-check' "$OUT/stress_ps2.log" | tail -1 | sed 's/.*rensga: //')"
        row_ps2="| PS2 640x448 (PCSX2) | $(field "$qa" total_us) | $(field "$qa" render_us) | $(field "$qa" present_us) | ${qaa:-none} |"
      else
        echo "  PS2 half skipped (exit 77 equivalent): needs the ps2-build module, pcsx2 and a BIOS in ~/.config/rensga/emulators.json (local only)"
      fi
      { echo "### 1000 sprites, PSP vs PS2 (avg/max microseconds per frame over the last 60 frames)"
        echo "| Target | total_us | render_us | present_us | alloc-check |"; echo "|---|---|---|---|---|"
        echo "| PSP 480x272 (PPSSPPHeadless, software GE) | $(field "$pp" total_us) | $(field "$pp" render_us) | $(field "$pp" present_us) | $pa |"
        echo "$row_ps2"; echo; } >> "$EXLOG";;
    audio)
      step "audio: 16 voices + music at 222 and 333 MHz (F4.06)"
      { echo "### Audio stress, 16 looping effects + streamed music, 600 frames (PPSSPP virtual time)"
        echo "| CPU MHz | mix_us avg/max | blocks | cpu_permille | underruns | audio_us (prof) |"; echo "|---|---|---|---|---|---|"; } >> "$EXLOG"
      for mhz in 222 333; do
        psp_build -DRENSGA_PSP_AUDIO_STRESS=ON -DRENSGA_PSP_CPU_MHZ=$mhz -DRENSGA_EXIT_AFTER_FRAMES=600
        stage_root; boot software 90 "$OUT/audio_$mhz.log"
        need "rensga: audio-stress" "$OUT/audio_$mhz.log" "audio: no audio-stress line at $mhz MHz"
        need "rensga: alloc-check frames=[1-9][0-9]* allocs=0" "$OUT/audio_$mhz.log" "audio: alloc-check not 0 at $mhz MHz"
        l="$(grep -h 'audio-stress' "$OUT/audio_$mhz.log" | tail -1 | sed 's/.*rensga: audio-stress //')"
        echo "  $mhz MHz: $l"; echo "  $mhz MHz prof: $(lastprof "$OUT/audio_$mhz.log")"
        grep -h "clock\|cpu mhz\|cpu=" "$OUT/audio_$mhz.log" | head -2 | sed 's/^/  /'
        echo "| $mhz | $(field "$l" mix_us) | $(field "$l" blocks) | $(field "$l" cpu_permille) | $(field "$l" underruns) | $(field "$(lastprof "$OUT/audio_$mhz.log")" audio_us) |" >> "$EXLOG"
      done; echo >> "$EXLOG";;
    determinism)
      step "determinism 2D: Linux headless vs the PSP sim ELF, TitleBot"
      "$REPO/tools/ci/determinism.sh" 2d;;
  esac
done
[ -s "$EXLOG" ] && { echo; cat "$EXLOG"; }
echo "psp_2d: done ($MODES), output in $OUT"
