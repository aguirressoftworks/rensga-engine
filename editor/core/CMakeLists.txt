# Console-agnostic editor core (target seam, registry, project model, play controller).
find_package(Threads REQUIRED)
# Never names a console: tools/check_editor_core.sh enforces it.
add_library(rensga_editor_core STATIC
  src/target_registry.cpp src/editor_log.cpp src/project.cpp src/play_controller.cpp src/scene_document.cpp src/viewport.cpp
  src/script_assets.cpp src/script_build.cpp src/undo.cpp)
target_include_directories(rensga_editor_core PUBLIC include)
target_link_libraries(rensga_editor_core PUBLIC rensga_pipeline_headers rensga_cooker_lib
                      rensga_core nlohmann_json::nlohmann_json Threads::Threads
                      $<TARGET_NAME_IF_EXISTS:rensga_pipeline>)  # static cycle with pipeline is intended
rensga_host_target(rensga_editor_core)
