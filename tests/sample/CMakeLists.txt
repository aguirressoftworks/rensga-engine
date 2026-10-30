# F2.05: the one-room platformer sample (samples/platformer2d). The sample's scripts are compiled into the test
# (their own reflection group) so the cooker can read Room.scene; a bot drives the real game loop headless.
rensga_reflect_scan(sample_platformer RENSGA_PLATFORMER_SCRIPTS ${CMAKE_SOURCE_DIR}/samples/platformer2d/Assets/Scripts)
add_executable(rensga_platformer_tests ../test_main.cpp test_room_bot.cpp test_room.cpp test_gameplay.cpp test_ddol.cpp ${RENSGA_PLATFORMER_SCRIPTS})
target_link_libraries(rensga_platformer_tests PRIVATE rensga_cooker_lib rensga_api)
target_include_directories(rensga_platformer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_SOURCE_DIR}/samples/platformer2d/Assets/Scripts)
target_compile_definitions(rensga_platformer_tests PRIVATE RENSGA_SAMPLE_DIR="${CMAKE_SOURCE_DIR}/samples/platformer2d")
rensga_runtime_target(rensga_platformer_tests)   # scripts derive from Behaviour (-fno-rtti)
add_test(NAME rensga_platformer_tests COMMAND rensga_platformer_tests)
