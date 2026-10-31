# Pipeline end-to-end checks (host only, no Docker, no emulator).
if(TARGET rensga_headless AND TARGET rensga-cooker AND TARGET rensga)
  set(PIPE_PACK ${RENSGA_PROJECT_DIR}/Library/Cooked/psp/game.rpak)
  # Cook the template, then run the cooked pack headless with a scripted walk: a pickup is collected.
  add_test(NAME pipeline_cooked_pack_pickup COMMAND sh -c
    "'$<TARGET_FILE:rensga-cooker>' cook --project '${RENSGA_PROJECT_DIR}' --force && '$<TARGET_FILE:rensga_headless>' --pack '${PIPE_PACK}' --frames 120 --input '${CMAKE_BINARY_DIR}/tests/game/walk_pickup.json'")
  set_tests_properties(pipeline_cooked_pack_pickup PROPERTIES PASS_REGULAR_EXPRESSION "rensga: pickup 1/5")

  # CLI: validate emits JSON events for Validate and Cook; an over-budget project fails with non-zero exit.
  add_test(NAME cli_validate_json COMMAND $<TARGET_FILE:rensga> validate --target psp --json ${RENSGA_PROJECT_DIR})
  set_tests_properties(cli_validate_json PROPERTIES PASS_REGULAR_EXPRESSION "\"stage\":\"cook\",\"status\":\"succeeded\"")
  add_test(NAME cli_validate_over_budget COMMAND $<TARGET_FILE:rensga> validate --target psp --json ${CMAKE_SOURCE_DIR}/tests/fixtures/cook_over)
  set_tests_properties(cli_validate_over_budget PROPERTIES WILL_FAIL TRUE)
  add_test(NAME cli_unknown_target COMMAND $<TARGET_FILE:rensga> validate --target nope ${RENSGA_PROJECT_DIR})
  set_tests_properties(cli_unknown_target PROPERTIES WILL_FAIL TRUE)
endif()

# --- F5 emu-play begin ---
# F5.19-F5.22: launcher without an emulator (a shell script plays it), and the game side of Pause / Step.
if(TARGET rensga_target_psp AND TARGET rensga_editor_core)
  add_executable(rensga_emu_play_tests ../test_main.cpp test_emu_play.cpp)
  target_link_libraries(rensga_emu_play_tests PRIVATE rensga_target_psp rensga_editor_core)
  target_include_directories(rensga_emu_play_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_SOURCE_DIR}/editor/targets/psp/src)
  target_compile_definitions(rensga_emu_play_tests PRIVATE RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp")
  rensga_host_target(rensga_emu_play_tests)
  add_test(NAME pipeline_emu_play_launcher COMMAND rensga_emu_play_tests)
  set_tests_properties(pipeline_emu_play_launcher PROPERTIES TIMEOUT 120)
endif()
if(TARGET rensga_core)
  add_executable(rensga_devctl_tests ../test_main.cpp test_dev_control.cpp)
  target_link_libraries(rensga_devctl_tests PRIVATE rensga_core)
  target_include_directories(rensga_devctl_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_devctl_tests PRIVATE RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp")
  rensga_host_target(rensga_devctl_tests)
  add_test(NAME pipeline_devctl COMMAND rensga_devctl_tests)
endif()
# Real PPSSPP through the CLI (skips with 77 without xvfb-run, PPSSPPSDL or the psp-build module).
if(TARGET rensga)
  add_test(NAME pipeline_emu_play_cli COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/emu_play_cli.sh $<TARGET_FILE:rensga>
           ${CMAKE_SOURCE_DIR}/templates/psp-3d ${CMAKE_CURRENT_BINARY_DIR}/emu_play_cli)
  set_tests_properties(pipeline_emu_play_cli PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200 LABELS "slow;emulator" ENVIRONMENT "RENSGA_JOBS=2")
endif()
# --- F5 emu-play end ---
