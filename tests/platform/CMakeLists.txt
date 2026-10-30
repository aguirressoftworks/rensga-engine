# F2.01: touch controls. touch_pad_*: pure logic. touch_sdl_*: injects SDL finger events into SdlPlatform (needs a window,
# so it runs under xvfb-run and is skipped (77) without it).
if(TARGET rensga_platform_sdl)
  add_executable(rensga_touch_tests ${CMAKE_SOURCE_DIR}/tests/test_main.cpp test_touch.cpp)
  target_link_libraries(rensga_touch_tests PRIVATE rensga_core rensga_platform_sdl)
  target_include_directories(rensga_touch_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  rensga_runtime_target(rensga_touch_tests)
  add_test(NAME touch_pad_logic COMMAND rensga_touch_tests touch_pad_)
  find_program(RENSGA_XVFB_RUN xvfb-run)
  if(RENSGA_XVFB_RUN)
    add_test(NAME touch_sdl_events COMMAND ${CMAKE_COMMAND} -E env --unset=WAYLAND_DISPLAY SDL_VIDEODRIVER=x11
             ${RENSGA_XVFB_RUN} -a -s "-screen 0 1280x720x24" $<TARGET_FILE:rensga_touch_tests> touch_sdl_)
    set_tests_properties(touch_sdl_events PROPERTIES TIMEOUT 60 LABELS "gl")
  endif()
endif()

# F3.06: PSP pad mapping (pure) and SDL virtual controller + injected keys -> Input.
if(TARGET rensga_platform_sdl)
  add_executable(rensga_pad_tests ${CMAKE_SOURCE_DIR}/tests/test_main.cpp test_pad.cpp)
  target_link_libraries(rensga_pad_tests PRIVATE rensga_core rensga_platform_sdl)
  target_include_directories(rensga_pad_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests
                             ${CMAKE_SOURCE_DIR}/engine/backends/platform/psp/include)
  rensga_runtime_target(rensga_pad_tests)
  add_test(NAME pad_psp_map COMMAND rensga_pad_tests pad_psp_)
  if(RENSGA_XVFB_RUN)
    add_test(NAME pad_sdl_events COMMAND ${CMAKE_COMMAND} -E env --unset=WAYLAND_DISPLAY SDL_VIDEODRIVER=x11
             ${RENSGA_XVFB_RUN} -a -s "-screen 0 1280x720x24" $<TARGET_FILE:rensga_pad_tests> pad_sdl_)
    set_tests_properties(pad_sdl_events PROPERTIES TIMEOUT 60 LABELS "gl")
  endif()
endif()

# F2.03: desktop hot reload of a pack copy (skipped without a cooked pack or xvfb).
if(TARGET rensga_desktop)
  add_test(NAME hot_reload_desktop COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/hot_reload.sh $<TARGET_FILE:rensga_desktop>
           ${RENSGA_PROJECT_DIR}/Library/Cooked/psp/game.rpak)  # the pack of the project whose scripts are linked in
  set_tests_properties(hot_reload_desktop PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90 LABELS "slow;gl"
                      DEPENDS pipeline_cooked_pack_pickup)  # DEPENDS matches by test name across directories; no if(TEST) guard
endif()
