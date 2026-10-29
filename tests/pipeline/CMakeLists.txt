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
