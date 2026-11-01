# F5.14 GL: lit / skinned / fogged draws of the pack3d fixture under a virtual X server. Needs xvfb-run; GALLIUM_DRIVER=softpipe
# keeps it deterministic enough to compare. The test only checks that the picture is not blank and has colour from the lights.
if(TARGET rensga_render_gl AND TARGET rensga_platform_sdl)
  add_executable(rensga_gl_lit_shot lit_shot.cpp)
  target_link_libraries(rensga_gl_lit_shot PRIVATE rensga_api rensga_core rensga_render_gl rensga_platform_sdl)
  target_include_directories(rensga_gl_lit_shot PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  rensga_runtime_target(rensga_gl_lit_shot)
  add_executable(rensga_gl_room_shot room_shot.cpp)
  target_link_libraries(rensga_gl_room_shot PRIVATE rensga_api rensga_core rensga_render_gl rensga_platform_sdl)
  rensga_runtime_target(rensga_gl_room_shot)
  # --- F6 b2 begin ---
  add_executable(rensga_gl_fx_shot fx_shot.cpp)
  target_link_libraries(rensga_gl_fx_shot PRIVATE rensga_api rensga_core rensga_render_gl rensga_platform_sdl)
  rensga_runtime_target(rensga_gl_fx_shot)
  # --- F6 b2 end ---
  find_program(RENSGA_XVFB_RUN xvfb-run)
  if(RENSGA_XVFB_RUN)
    foreach(variant f16 c16)
      add_test(NAME gl_lit_${variant} COMMAND ${CMAKE_COMMAND} -E env --unset=WAYLAND_DISPLAY SDL_VIDEODRIVER=x11
               GALLIUM_DRIVER=softpipe ${RENSGA_XVFB_RUN} -a -s "-screen 0 1280x720x24" $<TARGET_FILE:rensga_gl_lit_shot>
               ${variant} ${CMAKE_CURRENT_BINARY_DIR}/lit_${variant}.png)
      set_tests_properties(gl_lit_${variant} PROPERTIES TIMEOUT 120 LABELS "slow;gl"
                           PASS_REGULAR_EXPRESSION "lit-check ok")
    endforeach()
    # --- F6 b2 begin ---
    add_test(NAME gl_fx_cameras_particles_lines COMMAND ${CMAKE_COMMAND} -E env --unset=WAYLAND_DISPLAY SDL_VIDEODRIVER=x11
             GALLIUM_DRIVER=softpipe ${RENSGA_XVFB_RUN} -a -s "-screen 0 1280x720x24" $<TARGET_FILE:rensga_gl_fx_shot>
             ${CMAKE_CURRENT_BINARY_DIR}/fx.png)
    set_tests_properties(gl_fx_cameras_particles_lines PROPERTIES TIMEOUT 120 LABELS "slow;gl" PASS_REGULAR_EXPRESSION "fx-check ok")
    # --- F6 b2 end ---
    # The 16-bit compact cube must decode to exactly the float cube's picture.
    add_test(NAME gl_lit_c16_matches_f16 COMMAND ${CMAKE_COMMAND} -E compare_files ${CMAKE_CURRENT_BINARY_DIR}/lit_f16.png
             ${CMAKE_CURRENT_BINARY_DIR}/lit_c16.png)
    set_tests_properties(gl_lit_c16_matches_f16 PROPERTIES DEPENDS "gl_lit_f16;gl_lit_c16" LABELS "slow;gl")
  endif()
endif()
