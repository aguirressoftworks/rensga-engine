# F5.11 / F5.18 (B4): profiler budget lines, the generated API pages, and the API tests on the host (the PSP run is tools/ci/psp_api.sh).
add_executable(rensga_profiler_tests ../test_main.cpp test_profiler.cpp)
target_link_libraries(rensga_profiler_tests PRIVATE rensga_core rensga_runtime_boot)
target_include_directories(rensga_profiler_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_SOURCE_DIR}/engine/runtime)
rensga_runtime_target(rensga_profiler_tests)
add_test(NAME profiler_budget COMMAND rensga_profiler_tests)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME api_pages_doc COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/docs/gen_api_pages.py --check)
  add_test(NAME psp_api_project COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/make_project.py ${CMAKE_CURRENT_BINARY_DIR}/project)
endif()

# The same ApiTests.cpp as a game on the host (cooks and runs rensga_headless; builds its own host tree, so it is slow).
if(RENSGA_TARGET STREQUAL "linux-host")
  add_test(NAME psp_api_host COMMAND ${CMAKE_COMMAND} -E env RENSGA_CI_OUT=${CMAKE_CURRENT_BINARY_DIR}/ci ${CMAKE_SOURCE_DIR}/tools/ci/psp_api.sh host)
  set_tests_properties(psp_api_host PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200 LABELS "slow;psp_api")
endif()
