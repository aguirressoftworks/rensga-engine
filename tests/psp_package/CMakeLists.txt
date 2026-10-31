# F5.03-F5.05: EBOOT package data (title, disc ID, version, art, licenses, music) and the PBP / PARAM.SFO reader.
if(TARGET rensga_target_psp AND TARGET rensga_editor_core)
  add_executable(rensga_psp_package_tests ../test_main.cpp test_package.cpp)
  target_link_libraries(rensga_psp_package_tests PRIVATE rensga_target_psp rensga_editor_core)
  target_include_directories(rensga_psp_package_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_SOURCE_DIR}/editor/targets/psp/src)
  target_compile_definitions(rensga_psp_package_tests PRIVATE RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp" RENSGA_SOURCE_DIR_T="${CMAKE_SOURCE_DIR}")
  rensga_host_target(rensga_psp_package_tests)
  add_test(NAME psp_package_data COMMAND rensga_psp_package_tests)
endif()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  # make_package_art.py (size, determinism, text), pbp_info.py on a synthetic PBP, the Package.json of both PSP templates.
  add_test(NAME psp_package_art COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/package_art_check.py ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/art)
endif()
# The real thing: rensga build --target psp with the native module on a copy of the 3D template, then the PBP is read back. 77 = no module.
if(TARGET rensga AND RENSGA_TARGET STREQUAL "linux-host")
  add_test(NAME psp_package_eboot COMMAND bash ${CMAKE_SOURCE_DIR}/tools/psp/package_check.sh $<TARGET_FILE:rensga> ${CMAKE_SOURCE_DIR}/templates/psp-3d ${CMAKE_CURRENT_BINARY_DIR}/eboot)
  set_tests_properties(psp_package_eboot PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1800 LABELS "slow;psp" ENVIRONMENT "RENSGA_JOBS=2")
endif()
# Window > Emulators and the install offer, on a fixture catalog under xvfb (77 = skipped).
if(TARGET rensga-editor AND RENSGA_TARGET STREQUAL "linux-host")
  add_test(NAME editor_emulator_manager_ui COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/emulator_ui_check.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/templates/psp-3d ${CMAKE_CURRENT_BINARY_DIR}/emulator_ui)
  set_tests_properties(editor_emulator_manager_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "slow;gl")
endif()
# F5.09: Build and Run (release EBOOT in PPSSPPSDL), cold then warm (< 30 s). Needs xvfb, PPSSPPSDL and the psp-build module (77 otherwise).
if(TARGET rensga-editor AND TARGET rensga AND RENSGA_TARGET STREQUAL "linux-host")
  add_test(NAME editor_build_run COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/build_run_ui.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/templates/psp-3d ${CMAKE_CURRENT_BINARY_DIR}/build_run)
  set_tests_properties(editor_build_run PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 2400 LABELS "slow;gl;emulator" ENVIRONMENT "RENSGA_JOBS=2")
endif()
