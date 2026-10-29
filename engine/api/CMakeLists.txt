add_library(rensga_api STATIC src/physics.cpp)
target_include_directories(rensga_api PUBLIC include)
target_link_libraries(rensga_api PUBLIC rensga_core)
rensga_runtime_target(rensga_api)
