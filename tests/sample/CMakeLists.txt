# F2.05 / F4.01: the platformer sample (samples/platformer2d): three rooms, lives, pause, results, saves. The sample's scripts are
# compiled into the test (their own reflection group) so the cooker can read the scenes; a bot drives the real game loop headless.
rensga_reflect_scan(sample_platformer RENSGA_PLATFORMER_SCRIPTS ${CMAKE_SOURCE_DIR}/samples/platformer2d/Assets/Scripts)
add_executable(rensga_platformer_tests ../test_main.cpp test_room_bot.cpp test_room.cpp test_gameplay.cpp test_ddol.cpp test_flow.cpp test_ui_anchors.cpp
               test_ps2_icon.cpp test_profiles.cpp test_psp_project.cpp ${RENSGA_PLATFORMER_SCRIPTS})
target_link_libraries(rensga_platformer_tests PRIVATE rensga_cooker_lib rensga_api)
target_include_directories(rensga_platformer_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_SOURCE_DIR}/samples/platformer2d/Assets/Scripts
                           ${CMAKE_SOURCE_DIR}/engine/backends/platform/ps2/include)
target_compile_definitions(rensga_platformer_tests PRIVATE RENSGA_SAMPLE_DIR="${CMAKE_SOURCE_DIR}/samples/platformer2d"
                           RENSGA_SAMPLE_PSP_DIR="${CMAKE_SOURCE_DIR}/samples/platformer2d_psp")
rensga_runtime_target(rensga_platformer_tests)   # scripts derive from Behaviour (-fno-rtti)
add_test(NAME rensga_platformer_tests COMMAND rensga_platformer_tests)
