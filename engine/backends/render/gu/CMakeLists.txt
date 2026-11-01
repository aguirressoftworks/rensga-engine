add_library(rensga_render_gu STATIC src/gu_renderer.cpp src/gu_2d.cpp src/gu_vram.cpp src/gu_sprite.cpp src/gu_selftest.cpp src/gu_3d.cpp src/gu_fx.cpp)
target_include_directories(rensga_render_gu PUBLIC include)
target_link_libraries(rensga_render_gu PUBLIC rensga_core)  # PSP system libs are linked once, in order, by the executable
rensga_runtime_target(rensga_render_gu)
# On-target self test of the 2D path: -DRENSGA_GU_SELFTEST=ON draws a test panel over any project (gu_selftest.cpp).
option(RENSGA_GU_SELFTEST "GuRenderer: draw the 2D self-test panel (dev only)" OFF)
if(RENSGA_GU_SELFTEST)
  target_compile_definitions(rensga_render_gu PRIVATE RENSGA_GU_SELFTEST=1)
endif()
# Dev/CI hook: -DRENSGA_GU_DUMP_FRAME=N makes the renderer write host0:/rensga_shot.bmp itself when frame N starts.
set(RENSGA_GU_DUMP_FRAME 0 CACHE STRING "GuRenderer: frame that dumps host0:/rensga_shot.bmp (0 = never; main_psp dumps at exit)")
if(RENSGA_GU_DUMP_FRAME GREATER 0)
  target_compile_definitions(rensga_render_gu PRIVATE RENSGA_GU_DUMP_FRAME=${RENSGA_GU_DUMP_FRAME})
endif()
# F4.07: -DRENSGA_GU_DUMP_FRAMES="20,400,1000" writes host0:/rensga_shot_<frame>.bmp for each listed frame (one run, several screenshots).
set(RENSGA_GU_DUMP_FRAMES "" CACHE STRING "GuRenderer: comma-separated frames that dump host0:/rensga_shot_<frame>.bmp (empty = off)")
if(RENSGA_GU_DUMP_FRAMES)
  target_compile_definitions(rensga_render_gu PRIVATE "RENSGA_GU_DUMP_FRAMES=\"${RENSGA_GU_DUMP_FRAMES}\"")
endif()

# F5.01 dev probe: -DRENSGA_GU_PROBE=ON builds gu3d_probe.elf (PSP) that draws the pack3d_builder pack through drawMeshEx.
# Variants: -DRENSGA_GU_PROBE_DEFS="GU3D_CLIP;GU3D_SCALE". Matrix concatenation on the VFPU: -DRENSGA_PSP_VFPU_RENDER=ON/OFF (default ON).
option(RENSGA_GU_PROBE "GuRenderer: build the dev probe gu3d_probe (PSP only)" OFF)
set(RENSGA_GU_PROBE_DEFS "" CACHE STRING "GuRenderer probe: compile definitions of gu3d_probe")
option(RENSGA_PSP_VFPU_RENDER "GuRenderer: concatenate render-side matrices (decode x bone) on the VFPU (never used by sim code)" ON)
target_compile_definitions(rensga_render_gu PRIVATE RENSGA_PSP_VFPU_RENDER=$<BOOL:${RENSGA_PSP_VFPU_RENDER}>)
if(RENSGA_GU_PROBE)
  set(RENSGA_GU_PROBE_SRC ${CMAKE_CURRENT_LIST_DIR}/tools/gu3d_probe.cpp CACHE INTERNAL "")
  set(RENSGA_GU_PROBE_AUDIO ${CMAKE_SOURCE_DIR}/engine/runtime/runtime_audio.cpp CACHE INTERNAL "")
  function(_gu_probe_target)
    rensga_psp_executable(gu3d_probe SOURCES ${RENSGA_GU_PROBE_SRC} ${RENSGA_GU_PROBE_AUDIO} LIBS rensga_api rensga_core rensga_runtime_boot rensga_platform_psp rensga_render_gu rensga_audio_sdl)
    target_compile_definitions(gu3d_probe PRIVATE ${RENSGA_GU_PROBE_DEFS})
  endfunction()
  cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL _gu_probe_target)
endif()
option(RENSGA_GU_NOCULL "GuRenderer: dev, disable back-face culling of lit meshes" OFF)
if(RENSGA_GU_NOCULL)
  target_compile_definitions(rensga_render_gu PRIVATE RENSGA_GU_NOCULL=1)
endif()
