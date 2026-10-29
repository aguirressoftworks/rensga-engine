# Game/API tests: the template project's scripts are compiled in (static registration), like the runtime.
set(RENSGA_PROJECT_SCRIPTS_T ${RENSGA_REFLECT_PROJECT_SRCS})
rensga_reflect_scan(tests_game RENSGA_GAME_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/test_game.cpp)
add_executable(rensga_game_tests ${CMAKE_SOURCE_DIR}/tests/test_main.cpp ${RENSGA_GAME_TESTS} ${RENSGA_PROJECT_SCRIPTS_T})
target_link_libraries(rensga_game_tests PRIVATE rensga_api rensga_core)
target_include_directories(rensga_game_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
rensga_runtime_target(rensga_game_tests)
add_test(NAME rensga_game_tests COMMAND rensga_game_tests)

# CLI-level checks of rensga_headless on the programmatic level.
if(TARGET rensga_headless)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/walk_pickup.json
    "{\"steps\":[{\"frame\":0,\"x\":0,\"y\":1,\"buttons\":[]},{\"frame\":90,\"x\":0,\"y\":0,\"buttons\":[]}]}")
  add_test(NAME headless_pickup COMMAND rensga_headless --scene-test --frames 120 --input ${CMAKE_CURRENT_BINARY_DIR}/walk_pickup.json --hash)
  set_tests_properties(headless_pickup PROPERTIES PASS_REGULAR_EXPRESSION "rensga: pickup 1/5")
endif()

# Host-side check that TestLevel.scene parses and matches the level built in code.
if(TARGET rensga_reflect_json)
  add_executable(rensga_scene_tests ${CMAKE_SOURCE_DIR}/tests/test_main.cpp scene_check/check_scene.cpp scene_check/check_roundtrip.cpp ${RENSGA_PROJECT_SCRIPTS_T})
  target_link_libraries(rensga_scene_tests PRIVATE rensga_api rensga_core rensga_reflect_json)
  target_include_directories(rensga_scene_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
  target_compile_definitions(rensga_scene_tests PRIVATE RENSGA_PROJECT_DIR_STR="${RENSGA_PROJECT_DIR}")
  rensga_host_target(rensga_scene_tests)
  target_compile_options(rensga_scene_tests PRIVATE -fno-rtti)  # scripts derive from engine classes
  add_test(NAME rensga_scene_tests COMMAND rensga_scene_tests)
endif()

if(TARGET rensga_headless)
  add_test(NAME headless_hash_deterministic COMMAND sh -c
    "a=$('$<TARGET_FILE:rensga_headless>' --scene-test --frames 200 --input ${CMAKE_CURRENT_BINARY_DIR}/walk_pickup.json --hash | grep hash); b=$('$<TARGET_FILE:rensga_headless>' --scene-test --frames 200 --input ${CMAKE_CURRENT_BINARY_DIR}/walk_pickup.json --hash | grep hash); test -n \"$a\" && test \"$a\" = \"$b\"")
endif()
