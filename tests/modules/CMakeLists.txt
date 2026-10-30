# Build support modules: installer, catalog, environment and toolchain resolver. No network: archives are built at test time
# and served through file:// URLs, with a temporary RENSGA_HOME-like modules dir.
if(TARGET rensga_modules AND TARGET rensga_toolchain)
  add_executable(rensga_modules_tests ../test_main.cpp test_modules.cpp)
  target_link_libraries(rensga_modules_tests PRIVATE rensga_modules rensga_toolchain)
  target_include_directories(rensga_modules_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_modules_tests PRIVATE
    RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp"
    RENSGA_TEST_CMAKE="${CMAKE_COMMAND}")
  rensga_host_target(rensga_modules_tests)
  add_test(NAME rensga_modules_tests COMMAND rensga_modules_tests)
  set_tests_properties(rensga_modules_tests PROPERTIES TIMEOUT 120)
endif()
