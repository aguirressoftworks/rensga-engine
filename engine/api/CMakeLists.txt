add_library(rensga_api STATIC src/physics.cpp src/hud.cpp src/sim.cpp src/testlevel.cpp)
target_include_directories(rensga_api PUBLIC include)
target_link_libraries(rensga_api PUBLIC rensga_core)
rensga_runtime_target(rensga_api)
