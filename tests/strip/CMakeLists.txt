# Editor metadata stripping (F1.05): the probe's generated table is compiled with RENSGA_STRIP_EDITOR_METADATA.
rensga_reflect_scan(tests_strip RENSGA_STRIP_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/probe_strip.cpp)
add_executable(rensga_strip_tests ../test_main.cpp ${RENSGA_STRIP_SRCS})
target_link_libraries(rensga_strip_tests PRIVATE rensga_api rensga_core)
target_include_directories(rensga_strip_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(rensga_strip_tests PRIVATE RENSGA_STRIP_EDITOR_METADATA)
rensga_runtime_target(rensga_strip_tests)
add_test(NAME rensga_strip_tests COMMAND rensga_strip_tests)
