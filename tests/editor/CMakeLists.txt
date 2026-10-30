# SceneDocument tests (host only; exceptions on).
if(TARGET rensga_editor_core)
  add_executable(rensga_editor_tests ../test_main.cpp test_scene_document.cpp test_target_module.cpp test_undo.cpp test_script_build.cpp test_game_module.cpp)
  target_link_libraries(rensga_editor_tests PRIVATE rensga_editor_core rensga_cooker_lib rensga_api)
  target_include_directories(rensga_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_editor_tests PRIVATE
    RENSGA_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
    RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp"
    RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp")
  rensga_link_whole_archive(rensga_editor_tests PRIVATE rensga_target_dummy)
  # The game module resolves engine symbols from the test executable, like the editor.
  rensga_link_whole_archive(rensga_editor_tests PRIVATE rensga_core)
  rensga_link_whole_archive(rensga_editor_tests PRIVATE rensga_api)
  set_target_properties(rensga_editor_tests PROPERTIES ENABLE_EXPORTS ON)
  rensga_host_target(rensga_editor_tests)
  add_test(NAME rensga_editor_tests COMMAND rensga_editor_tests)
  # Builds templates/psp's script module once; editor checks that need its scripts depend on this fixture.
  add_test(NAME editor_script_module_template COMMAND rensga_editor_tests script_module_template_build)
  set_tests_properties(editor_script_module_template PROPERTIES FIXTURES_SETUP script_module_psp TIMEOUT 900
                       ENVIRONMENT "RENSGA_JOBS=2")
  set_tests_properties(rensga_editor_tests PROPERTIES ENVIRONMENT "RENSGA_JOBS=2" TIMEOUT 1500)
endif()

# F3.02: asset database, watcher, import settings, VRAM layouts.
if(TARGET rensga_editor_core AND TARGET rensga_target_psp)
  add_executable(rensga_asset_tests ../test_main.cpp test_asset_database.cpp)
  target_link_libraries(rensga_asset_tests PRIVATE rensga_editor_core rensga_cooker_lib)
  target_include_directories(rensga_asset_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_asset_tests PRIVATE
    RENSGA_SAMPLE_DIR="${CMAKE_SOURCE_DIR}/samples/platformer2d"
    RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp"
    RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp_assets")
  rensga_link_whole_archive(rensga_asset_tests PRIVATE rensga_target_psp)
  rensga_host_target(rensga_asset_tests)
  add_test(NAME rensga_asset_tests COMMAND rensga_asset_tests)
endif()

# F3.02: Project window, asset inspector, VRAM map and the "import a texture" exercise under xvfb (skipped without it).
if(TARGET rensga-editor)
  add_test(NAME editor_assets_ui COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/assets_ui_check.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/samples/platformer2d ${CMAKE_SOURCE_DIR}/tests/fixtures/assets/palette12.png ${CMAKE_CURRENT_BINARY_DIR}/assets_ui)
  set_tests_properties(editor_assets_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 240 LABELS "slow;gl")
endif()
