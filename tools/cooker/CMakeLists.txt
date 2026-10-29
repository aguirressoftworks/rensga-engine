add_library(rensga_cooker_lib STATIC src/plugin_registry.cpp)
target_include_directories(rensga_cooker_lib PUBLIC include)
target_link_libraries(rensga_cooker_lib PUBLIC rensga_core rensga_reflect_json nlohmann_json::nlohmann_json)
rensga_host_target(rensga_cooker_lib)

add_executable(rensga-cooker src/main.cpp)
target_link_libraries(rensga-cooker PRIVATE rensga_cooker_lib)
rensga_host_target(rensga-cooker)
