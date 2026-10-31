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
    RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp-3d"
    RENSGA_TOUR_DIR="${CMAKE_SOURCE_DIR}/samples/editor_tour"
    RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp")
  rensga_link_whole_archive(rensga_editor_tests PRIVATE rensga_target_dummy)
  # The game module resolves engine symbols from the test executable, like the editor.
  rensga_link_whole_archive(rensga_editor_tests PRIVATE rensga_core)
  rensga_link_whole_archive(rensga_editor_tests PRIVATE rensga_api)
  set_target_properties(rensga_editor_tests PROPERTIES ENABLE_EXPORTS ON)
  rensga_host_target(rensga_editor_tests)
  add_test(NAME rensga_editor_tests COMMAND rensga_editor_tests)
  # Builds templates/psp-3d's script module once; editor checks that need its scripts depend on this fixture.
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
    RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp-3d"
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
      set(dir ${CMAKE_SOURCE_DIR}/templates/psp-3d)
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
           ${CMAKE_SOURCE_DIR}/templates/psp-3d ${CMAKE_CURRENT_BINARY_DIR}/play_hot_reload)
  set_tests_properties(editor_play_hot_reload PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500 LABELS "slow;gl" ENVIRONMENT "RENSGA_JOBS=2")
endif()

# F3.01 / F3.07 / gate F3.G2: the inspector model (OnValidate, Reset, CONTEXT_MENU, clipboard, multi-edit) and its undo/redo exactness.
if(TARGET rensga_editor_core)
  rensga_reflect_scan(tests_inspector RENSGA_INSPECTOR_TEST_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/test_inspector_model.cpp)
  add_executable(rensga_inspector_tests ../test_main.cpp ${RENSGA_INSPECTOR_TEST_SRCS})
  target_link_libraries(rensga_inspector_tests PRIVATE rensga_editor_core rensga_cooker_lib rensga_api)
  target_include_directories(rensga_inspector_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_inspector_tests PRIVATE RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp-3d")
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
    RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp-3d"
    RENSGA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp_scene")
  rensga_link_whole_archive(rensga_sceneedit_tests PRIVATE rensga_target_psp)
  rensga_host_target(rensga_sceneedit_tests)
  add_test(NAME rensga_sceneedit_tests COMMAND rensga_sceneedit_tests)
endif()

# F3.01 / F3.G1: hierarchy edits, 2D mode, ID-buffer picking, save and reopen a scene, under xvfb (skipped without it).
if(TARGET rensga-editor)
  add_test(NAME editor_scene_ui COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scene_ui_check.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/samples/platformer2d ${CMAKE_SOURCE_DIR}/templates/psp-3d ${CMAKE_CURRENT_BINARY_DIR}/scene_ui)
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
             ${CMAKE_SOURCE_DIR}/templates/psp-3d ${CMAKE_CURRENT_BINARY_DIR}/modules_ui)
    set_tests_properties(editor_modules_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "slow;gl")
  endif()
  set_tests_properties(rensga_editor_modules_tests PROPERTIES TIMEOUT 300)
endif()

# --- F4 2D tools (agent 2a) ---
# F4.08 part 2 (Editor 7): Tile Palette, 2D painting, Sprite Editor, SpriteRenderer sprite picker, on tests/fixtures/paint2d (a copy).
# Exit 77 = skipped (no xvfb-run / python3). `ctest -L f4_exercises` runs the roadmap exercise.
if(TARGET rensga-editor)
  set(PAINT2D ${CMAKE_SOURCE_DIR}/tests/fixtures/paint2d)
  add_test(NAME editor_tile_palette_ui COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tile_palette_ui_check.sh $<TARGET_FILE:rensga-editor>
           ${PAINT2D} ${CMAKE_CURRENT_BINARY_DIR}/tile_palette_ui)
  set_tests_properties(editor_tile_palette_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "slow;gl" ENVIRONMENT "RENSGA_JOBS=2")
  add_test(NAME editor_sprite_editor_ui COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/sprite_editor_ui_check.sh $<TARGET_FILE:rensga-editor>
           ${PAINT2D} ${CMAKE_CURRENT_BINARY_DIR}/sprite_editor_ui)
  set_tests_properties(editor_sprite_editor_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "slow;gl" ENVIRONMENT "RENSGA_JOBS=2")
  add_test(NAME exercise_paint_level COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/exercise_paint_level.sh $<TARGET_FILE:rensga-editor>
           ${PAINT2D} ${CMAKE_CURRENT_BINARY_DIR}/exercise_paint_level)
  set_tests_properties(exercise_paint_level PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500 LABELS "slow;gl;f4_exercises" ENVIRONMENT "RENSGA_JOBS=2")
endif()
# --- end 2a ---

# --- F4 hub templates (agent 2b) ---
# F4.09 / F4.10: the Hub template cards (hub.json), createProject per template, the generated 2D templates.
if(TARGET rensga_editor_core)
  add_executable(rensga_hub_tests ../test_main.cpp test_hub_templates.cpp)
  target_link_libraries(rensga_hub_tests PRIVATE rensga_editor_core)
  target_include_directories(rensga_hub_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_hub_tests PRIVATE RENSGA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
                             RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp_hub")
  rensga_host_target(rensga_hub_tests)
  add_test(NAME rensga_hub_tests COMMAND rensga_hub_tests)
  # templates/psp-2d and templates/ps2-2d are generated: the committed folders must equal the generator output.
  add_test(NAME templates_2d_generator COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/templates_2d_check.sh ${CMAKE_SOURCE_DIR})
  set_tests_properties(templates_2d_generator PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
# The Create Project dialog with its six cards, then a fresh PSP 2D project: opened, scripts built, Play in editor, Stop.
if(TARGET rensga-editor)
  add_test(NAME editor_hub_templates_ui COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/hub_templates_ui.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_BINARY_DIR}/editor_hub_templates_ui)
  set_tests_properties(editor_hub_templates_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500 LABELS "slow;gl" ENVIRONMENT "RENSGA_JOBS=2")
endif()
# --- end 2b ---

# --- F4.08 part 1 (agent core2d): tilemap editing model, rule tiles, sprite slices (no UI) ---
if(TARGET rensga_editor_core)
  add_executable(rensga_tilemap_tests ../test_main.cpp test_tilemap_edit.cpp test_sprite_slices.cpp test_rigidbody_pack.cpp)
  target_link_libraries(rensga_tilemap_tests PRIVATE rensga_editor_core rensga_cooker_lib)
  rensga_link_whole_archive(rensga_tilemap_tests PRIVATE rensga_core)   # the built-in components' reflection registrations (Rigidbody2D pack test)
  rensga_link_whole_archive(rensga_tilemap_tests PRIVATE rensga_api)
  target_include_directories(rensga_tilemap_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_tilemap_tests PRIVATE
    RENSGA_SAMPLE_DIR="${CMAKE_SOURCE_DIR}/samples/platformer2d"
    RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp_tilemap")
  rensga_host_target(rensga_tilemap_tests)
  add_test(NAME rensga_tilemap_tests COMMAND rensga_tilemap_tests)
endif()
# --- end core2d ---

# --- F5 emu-play begin ---
# F5.19-F5.22: Play on console in the editor with a real PPSSPPSDL under xvfb (Pause, Step N, Resume, Stop, x10, crash, hang, debugger).
if(TARGET rensga-editor AND TARGET rensga)
  add_test(NAME editor_emu_play COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/emu_play_ui.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/templates/psp-3d ${CMAKE_CURRENT_BINARY_DIR}/emu_play)
  set_tests_properties(editor_emu_play PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500 LABELS "slow;gl;emulator" ENVIRONMENT "RENSGA_JOBS=2")
endif()
# --- F5 emu-play end ---

# --- F5 B2 begin ---
# F5.06-F5.08: target rules as data (preview, import, inspector field gating) and the console-form cook (decoded textures, budgets,
# validation findings). One UI test drives the real editor on samples/psp3d_room (screenshots of the validation list, the dithered
# Game view with the device frame, and the import tab).
if(TARGET rensga_editor_core)
  add_executable(rensga_preview_rules_tests ../test_main.cpp test_preview_rules.cpp)
  target_link_libraries(rensga_preview_rules_tests PRIVATE rensga_editor_core rensga_cooker_lib rensga_api)
  target_include_directories(rensga_preview_rules_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_preview_rules_tests PRIVATE RENSGA_ROOM_DIR="${CMAKE_SOURCE_DIR}/samples/psp3d_room"
    RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp_preview")
  rensga_host_target(rensga_preview_rules_tests)
  add_test(NAME rensga_preview_rules_tests COMMAND rensga_preview_rules_tests)
endif()
if(TARGET rensga-editor)
  add_test(NAME editor_preview_ui COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/preview_ui_check.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/samples/psp3d_room ${CMAKE_CURRENT_BINARY_DIR}/preview_ui)
  set_tests_properties(editor_preview_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500 LABELS "slow;gl" ENVIRONMENT "RENSGA_JOBS=2")
endif()
# --- F5 B2 end ---

# --- F5 B3 begin ---
# F5.23 docked Emulator panel (X11 reparent, focus follows the pointer, Undock/Dock, floating and forced-Wayland fallbacks) and F5.10 Profiler
# panel + PPSSPP WebSocket debugger, with a real PPSSPPSDL under xvfb (rensga-xtool tree, X screenshots). 77 = skipped.
if(TARGET rensga-editor)
  add_test(NAME editor_profiler_feed_ui COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/profiler_feed_ui.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/templates/psp-3d ${CMAKE_CURRENT_BINARY_DIR}/profiler_feed)
  set_tests_properties(editor_profiler_feed_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300 LABELS "gl")
  if(TARGET rensga-xtool)
    add_test(NAME editor_dock_emulator_ui COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/dock_emulator_ui.sh $<TARGET_FILE:rensga-editor>
             ${CMAKE_SOURCE_DIR}/templates/psp-3d ${CMAKE_CURRENT_BINARY_DIR}/dock_emulator $<TARGET_FILE:rensga-xtool>)
    set_tests_properties(editor_dock_emulator_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1500 LABELS "slow;gl;emulator" ENVIRONMENT "RENSGA_JOBS=2")
  endif()
endif()
# --- F5 B3 end ---

# --- F6 E0 begin ---
# F6 contract: ProjectSettings schemas, prefab file format, new asset kinds and creation stubs (editor/core only, no UI).
if(TARGET rensga_editor_core AND TARGET rensga_target_psp)
  add_executable(rensga_f6_contract_tests ../test_main.cpp test_project_settings.cpp test_prefab_format.cpp)
  target_link_libraries(rensga_f6_contract_tests PRIVATE rensga_editor_core rensga_cooker_lib)
  target_include_directories(rensga_f6_contract_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
  target_compile_definitions(rensga_f6_contract_tests PRIVATE
    RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp-3d"
    RENSGA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}/tmp_f6_contract")
  rensga_link_whole_archive(rensga_f6_contract_tests PRIVATE rensga_target_psp)
  rensga_host_target(rensga_f6_contract_tests)
  add_test(NAME rensga_f6_contract_tests COMMAND rensga_f6_contract_tests)
endif()
# --- F6 E0 end ---

# --- F6 E0 begin (ui) ---
if(TARGET rensga-editor)
  add_test(NAME editor_f6_contract_ui COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/f6_contract_ui.sh $<TARGET_FILE:rensga-editor>
           ${CMAKE_SOURCE_DIR}/samples/platformer2d ${CMAKE_CURRENT_BINARY_DIR}/f6_contract_ui)
  set_tests_properties(editor_f6_contract_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 240 LABELS "slow;gl")
endif()
# --- F6 E0 end (ui) ---
