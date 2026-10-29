# SceneDocument tests (host only; exceptions on).
if(TARGET rensga_editor_core)
  add_executable(rensga_editor_tests ../test_main.cpp test_scene_document.cpp test_target_module.cpp)
  target_link_libraries(rensga_editor_tests PRIVATE rensga_editor_core rensga_cooker_lib rensga_api)
  target_include_directories(rensga_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_editor_tests PRIVATE
    RENSGA_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
    RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp"
    RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp")
  rensga_link_whole_archive(rensga_editor_tests PRIVATE rensga_target_dummy)
  rensga_host_target(rensga_editor_tests)
  add_test(NAME rensga_editor_tests COMMAND rensga_editor_tests)
endif()
