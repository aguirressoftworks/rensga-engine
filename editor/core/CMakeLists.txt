# Console-agnostic editor core (target seam, registry; the editor role adds project model, etc.).
# Never names a console: tools/check_editor_core.sh enforces it.
add_library(rensga_editor_core STATIC src/target_registry.cpp)
target_include_directories(rensga_editor_core PUBLIC include)
target_link_libraries(rensga_editor_core PUBLIC rensga_pipeline_headers rensga_cooker_lib)
rensga_host_target(rensga_editor_core)
