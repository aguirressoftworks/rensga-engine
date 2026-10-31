# Header-only view used by editor/core (TargetModule needs EventSink) to avoid a link cycle.
add_library(rensga_pipeline_headers INTERFACE)
target_include_directories(rensga_pipeline_headers INTERFACE include)

# POSIX child-process helper, also used by target modules.
add_library(rensga_process STATIC src/process.cpp)
target_link_libraries(rensga_process PUBLIC rensga_pipeline_headers)
find_package(Threads REQUIRED)
target_link_libraries(rensga_process PUBLIC Threads::Threads)
rensga_host_target(rensga_process)

# Where a build tool runs: native build support module, Docker (contributor fallback), or a clear error.
add_library(rensga_toolchain STATIC src/toolchain.cpp src/compile_cache.cpp)
target_link_libraries(rensga_toolchain PUBLIC rensga_pipeline_headers rensga_modules rensga_process)
rensga_host_target(rensga_toolchain)

add_library(rensga_pipeline STATIC src/pipeline.cpp)
target_link_libraries(rensga_pipeline PUBLIC rensga_pipeline_headers rensga_cooker_lib rensga_editor_core rensga_process rensga_toolchain)
rensga_host_target(rensga_pipeline)
