# Runtime executables. Project scripts (<project>/Assets/Scripts/*.cpp) are compiled as SOURCES of the
# executables (not a static lib) so their static REFLECT registrars survive linking.
set(RENSGA_PROJECT_SCRIPTS ${RENSGA_REFLECT_PROJECT_SRCS})  # generated tables replace the reflected .cpp files

# F4 contract: the shared pack boot (runtime_boot.h) and the frame profiler. Core only. Mains link it as they adopt it;
# bootPack with BootDeps::audio set also needs runtime_audio.cpp in the executable (it already is on desktop and PS2).
add_library(rensga_runtime_boot STATIC runtime_boot.cpp profiler.cpp prof_stream.cpp runtime_budget.cpp)
target_include_directories(rensga_runtime_boot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rensga_runtime_boot PUBLIC rensga_core)
rensga_runtime_target(rensga_runtime_boot)
# --- F6 d2 begin ---
# F6.06 remote profiler: localhost TCP server on Linux and Android players (Windows streams through the log channel);
# RENSGA_PROF_STREAM_LOG=ON (dev builds of the pipeline) writes one "rensga: pf ..." line per frame to the log channel.
option(RENSGA_PROF_STREAM_LOG "Dev build: stream one 'rensga: pf' profiler line per frame to the log channel" OFF)
if(RENSGA_TARGET MATCHES "^(linux-host|android)$")
  target_compile_definitions(rensga_runtime_boot PRIVATE RENSGA_PROF_SOCKET=1)
endif()
if(RENSGA_PROF_STREAM_LOG)
  target_compile_definitions(rensga_runtime_boot PRIVATE RENSGA_PROF_STREAM_LOG=1)
endif()
# --- F6 d2 end ---

# PSP executables (F4.03): link our libraries, then the ordered system libraries once (RENSGA_PSP_SYSLIBS, root CMakeLists.txt), fix up
# the imports (PPSSPP and hardware both need it) and, with RENSGA_PSP_PRX, link a PRX module for the EBOOT.PBP Package stage.
#   rensga_psp_executable(<target> SOURCES <files...> LIBS <our libs...>)
if(RENSGA_TARGET STREQUAL "psp")
  set(RENSGA_EXIT_AFTER_FRAMES 0 CACHE STRING "PSP: exit the game after N frames (screenshots/CI); 0 = never")
  option(RENSGA_PSP_PRX "Link the PSP executables as PRX and generate <name>.elf.prx" OFF)
endif()
function(rensga_psp_executable target)
  cmake_parse_arguments(P "" "" "SOURCES;LIBS" ${ARGN})
  add_executable(${target} ${P_SOURCES})
  target_link_libraries(${target} PRIVATE ${P_LIBS} ${RENSGA_PSP_SYSLIBS})
  target_include_directories(${target} PRIVATE ${RENSGA_GENERATED_DIR})
  rensga_runtime_target(${target})
  if(RENSGA_EXIT_AFTER_FRAMES GREATER 0)
    target_compile_definitions(${target} PRIVATE RENSGA_EXIT_AFTER_FRAMES=${RENSGA_EXIT_AFTER_FRAMES})
  endif()
  set_target_properties(${target} PROPERTIES SUFFIX ".elf")
  target_link_options(${target} PRIVATE -Wl,-q)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND "$ENV{PSPDEV}/bin/psp-fixup-imports" "$<TARGET_FILE:${target}>"
    COMMENT "psp-fixup-imports ${target}")
  if(RENSGA_PSP_PRX)
    target_link_options(${target} PRIVATE
      -specs=$ENV{PSPDEV}/psp/sdk/lib/prxspecs
      -Wl,-T$ENV{PSPDEV}/psp/sdk/lib/linkfile.prx
      $ENV{PSPDEV}/psp/sdk/lib/prxexports.o)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "$ENV{PSPDEV}/bin/psp-prxgen" "$<TARGET_FILE:${target}>" "$<TARGET_FILE:${target}>.prx"
      COMMENT "psp-prxgen ${target}")
  endif()
endfunction()

# Engine 0 hello: one main on linux-host (needs SDL2), windows, android, ps2 and psp (rensga_hello.elf, the hello EBOOT exercise).
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_hello.cpp AND RENSGA_TARGET STREQUAL "psp")
  rensga_psp_executable(rensga_hello SOURCES main_hello.cpp LIBS rensga_core rensga_platform_psp rensga_render_gu)
  target_compile_definitions(rensga_hello PRIVATE RENSGA_PLATFORM_PSP)
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_hello.cpp)
  if(RENSGA_TARGET STREQUAL "android")
    add_library(rensga_hello SHARED main_hello.cpp)  # the APK loads the player as libmain.so (below); hello stays as libhello_main.so
    set_target_properties(rensga_hello PROPERTIES OUTPUT_NAME hello_main)
    target_link_libraries(rensga_hello PRIVATE log)
  elseif(RENSGA_TARGET STREQUAL "ps2")
    add_executable(rensga_hello main_hello.cpp)
    target_compile_definitions(rensga_hello PRIVATE RENSGA_PLATFORM_PS2)
    target_link_libraries(rensga_hello PRIVATE rensga_platform_ps2 rensga_render_gs gskit dmakit c)
    set_target_properties(rensga_hello PROPERTIES SUFFIX ".elf")
  elseif(TARGET rensga_platform_sdl)
    add_executable(rensga_hello main_hello.cpp)
    if(RENSGA_TARGET STREQUAL "windows")
      target_link_options(rensga_hello PRIVATE -static-libgcc -static-libstdc++)
    endif()
  endif()
  if(TARGET rensga_hello)
    target_link_libraries(rensga_hello PRIVATE rensga_core)
    if(TARGET rensga_platform_sdl)
      target_link_libraries(rensga_hello PRIVATE rensga_platform_sdl rensga_render_gl)
    endif()
    rensga_runtime_target(rensga_hello)
  endif()
endif()

if(RENSGA_TARGET STREQUAL "psp")
  # PSP ELF. Backends come from engine/backends/*. Build options for the Stage 2 exercises (all default off):
  #   RENSGA_PSP_STRESS_SPRITES=<n>   draw n moving sprites on top of the scene (same stress as rensga_desktop --stress-sprites)
  #   RENSGA_PSP_AUDIO_STRESS=ON      16 looping effects + streamed music for RENSGA_EXIT_AFTER_FRAMES (default 600) frames, logs mix_us
  #   RENSGA_PSP_AUTOSTART_SCENE=<s>  load scene <s> at frame 30 (no controller in emulator sandboxes)
  #   RENSGA_PSP_CPU_MHZ=222|333      (platform/psp) CPU clock; RENSGA_EXIT_AFTER_FRAMES=<n> exits after n frames (CI)
  set(RENSGA_PSP_STRESS_SPRITES 0 CACHE STRING "PSP: sprites drawn per frame by rensga_game (stress test, 0 = off)")
  option(RENSGA_PSP_AUDIO_STRESS "PSP: audio stress (16 looping effects + streamed music), logs mix_us" OFF)
  set(RENSGA_PSP_AUTOSTART_SCENE "" CACHE STRING "PSP: scene name to load at frame 30")
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_psp.cpp)
    set(_psp_libs rensga_api rensga_core rensga_runtime_boot rensga_platform_psp rensga_render_gu)
    if(TARGET rensga_audio_sdl)
      list(APPEND _psp_libs rensga_audio_sdl)
    endif()
    rensga_psp_executable(rensga_game SOURCES main_psp.cpp runtime_audio.cpp ${RENSGA_PROJECT_SCRIPTS} LIBS ${_psp_libs})
    set_target_properties(rensga_game PROPERTIES OUTPUT_NAME rensga_game)
    target_compile_definitions(rensga_game PRIVATE RENSGA_STRESS_MAX=4096)
    if(RENSGA_PSP_STRESS_SPRITES GREATER 0)
      target_compile_definitions(rensga_game PRIVATE RENSGA_PSP_STRESS_SPRITES=${RENSGA_PSP_STRESS_SPRITES})
    endif()
    if(RENSGA_PSP_AUDIO_STRESS)
      target_compile_definitions(rensga_game PRIVATE RENSGA_PSP_AUDIO_STRESS=1)
    endif()
    if(RENSGA_PSP_AUTOSTART_SCENE)
      target_compile_definitions(rensga_game PRIVATE "RENSGA_PSP_AUTOSTART_SCENE=\"${RENSGA_PSP_AUTOSTART_SCENE}\"")
    endif()
  endif()
  # PSP simulation ELF (F2.G2): same GameLoop + pack + input as rensga_headless, no rendering, logs the state hash.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_psp_sim.cpp)
    rensga_psp_executable(rensga_sim SOURCES main_psp_sim.cpp runtime_audio_null.cpp ${RENSGA_PROJECT_SCRIPTS}
                          LIBS rensga_api rensga_core rensga_runtime_boot rensga_platform_psp)
  endif()
elseif(RENSGA_TARGET STREQUAL "linux-host")
  # Linux-host simulation: null backends + stdio file system, no window.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_headless.cpp)
    add_executable(rensga_headless main_headless.cpp runtime_audio_null.cpp ${RENSGA_PROJECT_SCRIPTS})
    target_link_libraries(rensga_headless PRIVATE rensga_api rensga_core rensga_runtime_boot)
    target_include_directories(rensga_headless PRIVATE ${RENSGA_GENERATED_DIR})
    rensga_runtime_target(rensga_headless)
  endif()
endif()

# Desktop player (Engine 2): SDL platform + GL renderer. linux-host and windows are executables; on android it
# builds as the native library `libmain.so` that SDLActivity loads (the APK runs the player; F2.01). Project scripts are compiled in, like the other runtimes.
if(TARGET rensga_platform_sdl AND TARGET rensga_render_gl AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_desktop.cpp)
  if(RENSGA_TARGET STREQUAL "android")
    add_library(rensga_desktop SHARED main_desktop.cpp runtime_audio.cpp ${RENSGA_PROJECT_SCRIPTS})
    set_target_properties(rensga_desktop PROPERTIES OUTPUT_NAME main)  # SDLActivity loads libmain.so (F2.01: the APK runs the player)
    target_link_libraries(rensga_desktop PRIVATE log)
  else()
    add_executable(rensga_desktop main_desktop.cpp runtime_audio.cpp ${RENSGA_PROJECT_SCRIPTS})
    if(RENSGA_TARGET STREQUAL "windows")
      target_link_options(rensga_desktop PRIVATE -static-libgcc -static-libstdc++)
    endif()
  endif()
  target_link_libraries(rensga_desktop PRIVATE rensga_api rensga_core rensga_runtime_boot rensga_platform_sdl rensga_render_gl rensga_audio_sdl)
  target_include_directories(rensga_desktop PRIVATE ${RENSGA_GENERATED_DIR})
  rensga_runtime_target(rensga_desktop)
endif()

# PS2 game (gsKit renderer, libpad platform, cdrom0:/host: file system). The gs role owns main_ps2.cpp.
if(RENSGA_TARGET STREQUAL "ps2" AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_ps2.cpp)
  add_executable(rensga_ps2 main_ps2.cpp runtime_audio.cpp ${RENSGA_PROJECT_SCRIPTS})
  target_include_directories(rensga_ps2 PRIVATE ${RENSGA_GENERATED_DIR})
  target_link_libraries(rensga_ps2 PRIVATE rensga_api rensga_core rensga_runtime_boot rensga_platform_ps2 rensga_audio_ps2 rensga_render_gs gskit dmakit c)
  set(RENSGA_PS2_STRESS_SPRITES 1000 CACHE STRING "PS2: sprites drawn per frame by rensga_ps2 (stress test)")
  set(RENSGA_PS2_CHECK_FRAMES 300 CACHE STRING "PS2: frame at which the alloc-check line is logged")
  set(RENSGA_PS2_AUDIO_STRESS 0 CACHE STRING "PS2: run the audio stress (streamed music + 16 looping effects) for N frames, then exit (0 = off)")
  set(RENSGA_PS2_SHOT_FRAME 0 CACHE STRING "PS2: dump the frame buffers to host: at this frame (0 = off)")
  target_compile_definitions(rensga_ps2 PRIVATE RENSGA_PLATFORM_PS2 RENSGA_PS2_STRESS_SPRITES=${RENSGA_PS2_STRESS_SPRITES}
    RENSGA_PS2_CHECK_FRAMES=${RENSGA_PS2_CHECK_FRAMES})
  set(RENSGA_PS2_AUTOSTART "" CACHE STRING "PS2 game mode: scene name to load at frame 30 (no controller in emulator sandboxes)")
  option(RENSGA_PS2_VRAM_STRESS "PS2: draw 24 textures (more than the VRAM texture area) every frame" OFF)
  if(RENSGA_PS2_VRAM_STRESS)
    target_compile_definitions(rensga_ps2 PRIVATE RENSGA_PS2_VRAM_STRESS=1)
  endif()
  option(RENSGA_PS2_SAVE_SELFTEST "PS2 game mode: write and read back save:/selftest at frame 20" OFF)
  if(RENSGA_PS2_AUTOSTART)
    target_compile_definitions(rensga_ps2 PRIVATE "RENSGA_PS2_AUTOSTART=\"${RENSGA_PS2_AUTOSTART}\"")
  endif()
  if(RENSGA_PS2_SAVE_SELFTEST)
    target_compile_definitions(rensga_ps2 PRIVATE RENSGA_PS2_SAVE_SELFTEST=1)
  endif()
  option(RENSGA_PS2_FORMAT_BLANK_CARD "PS2 emulator test builds only: format an UNFORMATTED memory card instead of disabling saves" OFF)
  if(RENSGA_PS2_FORMAT_BLANK_CARD)
    target_compile_definitions(rensga_platform_ps2 PRIVATE RENSGA_PS2_FORMAT_BLANK_CARD=1)
  endif()
  if(RENSGA_PS2_AUDIO_STRESS GREATER 0)
    target_compile_definitions(rensga_ps2 PRIVATE RENSGA_PS2_AUDIO_STRESS=${RENSGA_PS2_AUDIO_STRESS})
  endif()
  if(RENSGA_PS2_SHOT_FRAME GREATER 0)
    target_compile_definitions(rensga_ps2 PRIVATE RENSGA_PS2_SHOT_FRAME=${RENSGA_PS2_SHOT_FRAME})
  endif()
  set_target_properties(rensga_ps2 PROPERTIES SUFFIX ".elf")
  rensga_runtime_target(rensga_ps2)
endif()
