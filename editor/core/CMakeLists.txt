# Console-agnostic editor core (target seam, registry, project model, play controller).
find_package(Threads REQUIRED)
# Never names a console: tools/check_editor_core.sh enforces it.
add_library(rensga_editor_core STATIC
  src/target_registry.cpp src/editor_log.cpp src/project.cpp src/play_controller.cpp src/scene_document.cpp src/viewport.cpp
  src/script_assets.cpp src/script_build.cpp src/editor_prefs.cpp src/undo.cpp src/hub.cpp)
target_sources(rensga_editor_core PRIVATE src/asset_database.cpp src/import_settings.cpp src/file_watcher.cpp src/vram_map.cpp)  # F3.02
target_sources(rensga_editor_core PRIVATE src/game_module.cpp)  # F3.03 (added by the F3.02 build until the hot-reload change lists it)
target_include_directories(rensga_editor_core PUBLIC include)
target_link_libraries(rensga_editor_core PUBLIC rensga_pipeline_headers rensga_cooker_lib
                      rensga_core nlohmann_json::nlohmann_json Threads::Threads
                      $<TARGET_NAME_IF_EXISTS:rensga_pipeline>)  # static cycle with pipeline is intended
# Game module (F3.03): where the script module template and the engine headers live, and how to build it.
target_compile_definitions(rensga_editor_core PRIVATE RENSGA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  RENSGA_CMAKE_COMMAND="${CMAKE_COMMAND}" RENSGA_SCRIPT_BUILD_TYPE="$<IF:$<BOOL:$<CONFIG>>,$<CONFIG>,Debug>")
target_link_libraries(rensga_editor_core PUBLIC ${CMAKE_DL_LIBS})
rensga_host_target(rensga_editor_core)
