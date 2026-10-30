# SceneDocument tests (host only; exceptions on).
if(TARGET rensga_editor_core)
  rensga_reflect_scan(tests_play RENSGA_PLAY_TEST_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/test_play_session.cpp)  # PlayMover (F3.04)
  # F3.07 edit-mode World: EditProbe / EditDormant plus the real Patrol script of samples/editor_tour
  rensga_reflect_scan(tests_editmode RENSGA_EDITMODE_TEST_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/test_edit_mode_world.cpp
                      ${CMAKE_SOURCE_DIR}/samples/editor_tour/Assets/Scripts/Patrol.cpp)
  add_executable(rensga_editor_tests ../test_main.cpp test_scene_document.cpp test_target_module.cpp test_undo.cpp test_script_build.cpp test_game_module.cpp ${RENSGA_PLAY_TEST_SRCS} ${RENSGA_EDITMODE_TEST_SRCS})
  target_link_libraries(rensga_editor_tests PRIVATE rensga_editor_core rensga_cooker_lib rensga_api)
  target_include_directories(rensga_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_editor_tests PRIVATE
    RENSGA_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
    RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp"
    RENSGA_TOUR_DIR="${CMAKE_SOURCE_DIR}/samples/editor_tour"
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

# F3.04 / gate F3.G3: Play in editor, pause, step, live edits, Stop restores the scene (xvfb; skips with 77 without it).
if(TARGET rensga-editor)
  foreach(proj IN ITEMS psp platformer)
    if(proj STREQUAL "psp")
      set(dir ${CMAKE_SOURCE_DIR}/templates/psp)
      set(args TestLevel Player W "2|speed|9")
    else()
      set(dir ${CMAKE_SOURCE_DIR}/samples/platformer2d)
      set(args Room Player Right "0|sortingOrder|7")
    endif()
    add_test(NAME editor_play_${proj} COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/play_ui_check.sh $<TARGET_FILE:rensga-editor>
             ${dir} ${args} ${CMAKE_CURRENT_BINARY_DIR}/play_${proj})
    set_tests_properties(editor_play_${proj} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500 LABELS "slow;gl"
                         ENVIRONMENT "RENSGA_JOBS=2")
  endforeach()
  add_test(NAME editor_play_hot_reload COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/play_hot_reload.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/templates/psp ${CMAKE_CURRENT_BINARY_DIR}/play_hot_reload)
  set_tests_properties(editor_play_hot_reload PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500 LABELS "slow;gl" ENVIRONMENT "RENSGA_JOBS=2")
endif()

# F3.01 / F3.07 / gate F3.G2: the inspector model (OnValidate, Reset, CONTEXT_MENU, clipboard, multi-edit) and its undo/redo exactness.
if(TARGET rensga_editor_core)
  rensga_reflect_scan(tests_inspector RENSGA_INSPECTOR_TEST_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/test_inspector_model.cpp)
  add_executable(rensga_inspector_tests ../test_main.cpp ${RENSGA_INSPECTOR_TEST_SRCS})
  target_link_libraries(rensga_inspector_tests PRIVATE rensga_editor_core rensga_cooker_lib rensga_api)
  target_include_directories(rensga_inspector_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_inspector_tests PRIVATE RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp")
  rensga_host_target(rensga_inspector_tests)
  add_test(NAME rensga_inspector_tests COMMAND rensga_inspector_tests)
endif()

# F3.01: the inspector in the real editor under xvfb (screenshots land in the build dir; skipped without xvfb).
if(TARGET rensga-editor)
  add_test(NAME editor_inspector_ui COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/inspector_ui_check.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/tests/fixtures/inspector_demo ${CMAKE_CURRENT_BINARY_DIR}/inspector_ui)
  set_tests_properties(editor_inspector_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "slow;gl" ENVIRONMENT "RENSGA_JOBS=2")
endif()

# F3.01 / gate F3.G1: multi-select edits, the 2D layer, built-in gizmos, scenes saved twice without a diff.
if(TARGET rensga_editor_core AND TARGET rensga_target_psp)
  add_executable(rensga_sceneedit_tests ../test_main.cpp test_scene_edit.cpp)
  target_link_libraries(rensga_sceneedit_tests PRIVATE rensga_editor_core rensga_cooker_lib)
  target_include_directories(rensga_sceneedit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_sceneedit_tests PRIVATE
    RENSGA_SAMPLE_DIR="${CMAKE_SOURCE_DIR}/samples/platformer2d"
    RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp"
    RENSGA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp_scene")
  rensga_link_whole_archive(rensga_sceneedit_tests PRIVATE rensga_target_psp)
  rensga_host_target(rensga_sceneedit_tests)
  add_test(NAME rensga_sceneedit_tests COMMAND rensga_sceneedit_tests)
endif()

# F3.01 / F3.G1: hierarchy edits, 2D mode, ID-buffer picking, save and reopen a scene, under xvfb (skipped without it).
if(TARGET rensga-editor)
  add_test(NAME editor_scene_ui COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scene_ui_check.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/samples/platformer2d ${CMAKE_SOURCE_DIR}/templates/psp ${CMAKE_CURRENT_BINARY_DIR}/scene_ui)
  set_tests_properties(editor_scene_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "slow;gl")
endif()

# F3 roadmap exercises (label f3_exercises, `ctest -L f3_exercises`): each one drives the real editor / player under xvfb on a
# temp copy of samples/editor_tour (or samples/platformer2d) and leaves a screenshot in the work dir. Exit 77 = skipped.
#   Editor 2 save and reopen a scene | Editor 3 import a texture (editor_assets_ui) | Editor 4 hot-reload a Rotator
#   Editor 5 play and revert | API 4 coroutine door | API 5 input test scene | API 11 patrol script with gizmos
if(TARGET rensga-editor)
  set(RENSGA_EXERCISE_SAMPLE_BUILD "${CMAKE_BINARY_DIR}/exercise_sample" CACHE PATH "Project build tree for the API 5 exercise (incremental when it exists)")
  set(TOUR ${CMAKE_SOURCE_DIR}/samples/editor_tour)
  foreach(ex IN ITEMS scene_roundtrip hot_reload play_revert door patrol)
    add_test(NAME exercise_${ex} COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/exercise_${ex}.sh $<TARGET_FILE:rensga-editor> ${TOUR}
             ${CMAKE_CURRENT_BINARY_DIR}/exercise_${ex})
    set_tests_properties(exercise_${ex} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500 LABELS "slow;gl;f3_exercises" ENVIRONMENT "RENSGA_JOBS=2")
  endforeach()
  set_tests_properties(editor_assets_ui PROPERTIES LABELS "slow;gl;f3_exercises")   # Editor 3 "import a texture"
  add_test(NAME exercise_input_desktop COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/exercise_input_desktop.sh ${CMAKE_SOURCE_DIR}
           ${RENSGA_EXERCISE_SAMPLE_BUILD} ${CMAKE_SOURCE_DIR}/samples/platformer2d ${CMAKE_CURRENT_BINARY_DIR}/exercise_input_desktop)
  set_tests_properties(exercise_input_desktop PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 3600 LABELS "slow;gl;f3_exercises" ENVIRONMENT "RENSGA_JOBS=2")
endif()

# F3.07 (API 11): edit-mode script callbacks in the real editor on samples/editor_tour (Patrol gizmos screenshot). Exit 77 = skipped.
if(TARGET rensga-editor)
  add_test(NAME editor_callbacks_ui COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/callbacks_ui_check.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/samples/editor_tour ${CMAKE_CURRENT_BINARY_DIR}/callbacks_ui)
  set_tests_properties(editor_callbacks_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500 LABELS "slow;gl;f3_exercises" ENVIRONMENT "RENSGA_JOBS=2")
endif()

# Hub > Modules model (rows, background install, license gate, target lookup): fixture archives, no network.
if(TARGET rensga_editor_core)
  add_executable(rensga_editor_modules_tests ../test_main.cpp test_modules_model.cpp)
  target_link_libraries(rensga_editor_modules_tests PRIVATE rensga_editor_core rensga_modules)
  target_include_directories(rensga_editor_modules_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_editor_modules_tests PRIVATE RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp")
  rensga_host_target(rensga_editor_modules_tests)
  add_test(NAME rensga_editor_modules_tests COMMAND rensga_editor_modules_tests)
  if(TARGET rensga-editor)
    add_test(NAME editor_modules_ui COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/modules_ui_check.sh $<TARGET_FILE:rensga-editor>
             ${CMAKE_SOURCE_DIR}/templates/psp ${CMAKE_CURRENT_BINARY_DIR}/modules_ui)
    set_tests_properties(editor_modules_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "slow;gl")
  endif()
  set_tests_properties(rensga_editor_modules_tests PROPERTIES TIMEOUT 300)
endif()
