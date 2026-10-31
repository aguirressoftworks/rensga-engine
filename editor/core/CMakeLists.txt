# Console-agnostic editor core (target seam, registry, project model, play controller).
find_package(Threads REQUIRED)
# Never names a console: tools/check_editor_core.sh enforces it.
add_library(rensga_editor_core STATIC
  src/target_registry.cpp src/editor_log.cpp src/project.cpp src/play_controller.cpp src/scene_document.cpp src/viewport.cpp
  src/script_assets.cpp src/script_build.cpp src/editor_prefs.cpp src/undo.cpp src/hub.cpp)
target_sources(rensga_editor_core PRIVATE src/asset_database.cpp src/import_settings.cpp src/file_watcher.cpp src/vram_map.cpp)  # F3.02
target_sources(rensga_editor_core PRIVATE src/game_module.cpp src/inspector_model.cpp)  # F3.03 / F3.01 inspector model (added by the F3.02 build until the hot-reload change lists it)
target_sources(rensga_editor_core PRIVATE src/scene2d.cpp src/document_gizmos.cpp)  # F3.01 scene (2D layer, built-in gizmos)
target_sources(rensga_editor_core PRIVATE src/editor_world.cpp src/play_session.cpp src/edit_mode_world.cpp)  # F3.04 Play in editor
target_sources(rensga_editor_core PRIVATE src/modules_model.cpp)  # Hub > Modules: list, install job, license gate (no ImGui)
target_sources(rensga_editor_core PRIVATE src/tilemap_edit.cpp src/sprite_slices.cpp)  # F4.08 Editor 7: tile editing and sprite slices (no UI)
target_include_directories(rensga_editor_core PUBLIC include)
target_link_libraries(rensga_editor_core PUBLIC rensga_pipeline_headers rensga_cooker_lib
                      rensga_core rensga_api rensga_modules nlohmann_json::nlohmann_json Threads::Threads
                      $<TARGET_NAME_IF_EXISTS:rensga_pipeline>)  # static cycle with pipeline is intended
# Game module (F3.03): where the script module template and the engine headers live, and how to build it.
target_compile_definitions(rensga_editor_core PRIVATE RENSGA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  RENSGA_CMAKE_COMMAND="${CMAKE_COMMAND}" RENSGA_SCRIPT_BUILD_TYPE="$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Debug>")
target_link_libraries(rensga_editor_core PUBLIC ${CMAKE_DL_LIBS})
rensga_host_target(rensga_editor_core)

# --- F6 E0 begin ---
target_sources(rensga_editor_core PRIVATE src/project_settings.cpp src/prefab_format.cpp src/asset_stubs.cpp)  # F6 contract: settings schemas, prefab format, asset stubs
# --- F6 E0 end ---
