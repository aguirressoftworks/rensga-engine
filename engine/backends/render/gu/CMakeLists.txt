add_library(rensga_render_gu STATIC src/gu_renderer.cpp src/gu_2d.cpp src/gu_vram.cpp)
target_include_directories(rensga_render_gu PUBLIC include)
target_link_libraries(rensga_render_gu PUBLIC rensga_core)  # PSP system libs are linked once, in order, by the executable
rensga_runtime_target(rensga_render_gu)
