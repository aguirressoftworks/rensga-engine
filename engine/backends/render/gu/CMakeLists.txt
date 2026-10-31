add_library(rensga_render_gu STATIC src/gu_renderer.cpp src/gu_2d.cpp src/gu_vram.cpp src/gu_sprite.cpp src/gu_selftest.cpp)
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
