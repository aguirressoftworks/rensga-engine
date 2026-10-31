# GL renderer logic that needs no GL context: sprite batch, letterbox, CLUT expansion.
if(TARGET rensga_render_gl)
  add_executable(rensga_render_tests ${CMAKE_SOURCE_DIR}/tests/test_main.cpp test_render.cpp)
  target_link_libraries(rensga_render_tests PRIVATE rensga_core rensga_render_gl)
  target_include_directories(rensga_render_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  rensga_runtime_target(rensga_render_tests)
  add_test(NAME rensga_render_tests COMMAND rensga_render_tests)

  # Desktop player smoke test: 1000 sprites under a virtual X server, alloc-check must report allocs=0.
  find_program(RENSGA_XVFB_RUN xvfb-run)
  if(RENSGA_XVFB_RUN AND TARGET rensga_desktop)
    add_test(NAME desktop_stress_sprites COMMAND ${CMAKE_COMMAND} -E env --unset=WAYLAND_DISPLAY SDL_VIDEODRIVER=x11
             ${RENSGA_XVFB_RUN} -a -s "-screen 0 1280x720x24" $<TARGET_FILE:rensga_desktop>
             --stress-sprites 1000 --frames 120 --screenshot ${CMAKE_CURRENT_BINARY_DIR}/sprites.png)
    set_tests_properties(desktop_stress_sprites PROPERTIES TIMEOUT 120 LABELS "slow;gl"
                         PASS_REGULAR_EXPRESSION "alloc-check frames=[0-9]+ allocs=0")
  endif()
endif()

# F4.04: host checks of the GU 2D backend's plain-C++ parts (sprite queue, VRAM allocator, layout). No PSP, no GL.
add_executable(rensga_gu_tests ${CMAKE_SOURCE_DIR}/tests/test_main.cpp test_gu_2d.cpp
               ${CMAKE_SOURCE_DIR}/engine/backends/render/gu/src/gu_sprite.cpp
               ${CMAKE_SOURCE_DIR}/engine/backends/render/gu/src/gu_vram.cpp)
target_link_libraries(rensga_gu_tests PRIVATE rensga_core)
target_include_directories(rensga_gu_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests ${CMAKE_SOURCE_DIR}/engine/backends/render/gu/include)
rensga_runtime_target(rensga_gu_tests)
add_test(NAME rensga_gu_tests COMMAND rensga_gu_tests)
