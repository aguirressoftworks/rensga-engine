# Build support modules: catalog, installer (curl + tar subprocesses), environment. Host tools only, no ImGui.
add_library(rensga_modules STATIC src/modules.cpp src/sha256.cpp)
target_include_directories(rensga_modules PUBLIC include)
target_link_libraries(rensga_modules PUBLIC rensga_process nlohmann_json::nlohmann_json)
target_compile_definitions(rensga_modules PRIVATE RENSGA_MODULES_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
rensga_host_target(rensga_modules)

# `rensga modules ...` command implementation, shared by the main CLI and the standalone rensga-modules binary.
add_library(rensga_modules_cmd STATIC cli/modules_cmd.cpp)
target_include_directories(rensga_modules_cmd PUBLIC cli)
target_link_libraries(rensga_modules_cmd PUBLIC rensga_modules)
rensga_host_target(rensga_modules_cmd)
add_executable(rensga-modules cli/main.cpp)
target_link_libraries(rensga-modules PRIVATE rensga_modules_cmd)
rensga_host_target(rensga-modules)
