add_library(rensga_render_gl STATIC src/gl_renderer.cpp)
target_include_directories(rensga_render_gl PUBLIC include)
target_link_libraries(rensga_render_gl PUBLIC rensga_core ${RENSGA_SDL2_TARGET})
rensga_runtime_target(rensga_render_gl)
