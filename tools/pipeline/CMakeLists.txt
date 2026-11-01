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

# F6.05: the platform table is data (tools/pipeline/data/platforms.json), embedded like the cooker's profile defaults.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/platforms.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/platforms.json" RENSGA_PLATFORMS_JSON)
configure_file(data/platforms.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/platforms_data.cpp" @ONLY)
add_library(rensga_pipeline STATIC src/pipeline.cpp src/platform_build.cpp src/process_launcher.cpp "${CMAKE_CURRENT_BINARY_DIR}/platforms_data.cpp")
target_compile_definitions(rensga_pipeline PRIVATE RENSGA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(rensga_pipeline PUBLIC rensga_pipeline_headers rensga_cooker_lib rensga_editor_core rensga_process rensga_toolchain)
rensga_host_target(rensga_pipeline)
