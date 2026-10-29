# Header-only view used by editor/core (TargetModule needs EventSink) to avoid a link cycle.
add_library(rensga_pipeline_headers INTERFACE)
target_include_directories(rensga_pipeline_headers INTERFACE include)

add_library(rensga_pipeline STATIC src/pipeline.cpp)
target_link_libraries(rensga_pipeline PUBLIC rensga_pipeline_headers rensga_cooker_lib rensga_editor_core)
rensga_host_target(rensga_pipeline)
