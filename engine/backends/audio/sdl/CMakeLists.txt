add_library(rensga_audio_sdl STATIC src/sdl_audio.cpp)
target_include_directories(rensga_audio_sdl PUBLIC include)
target_link_libraries(rensga_audio_sdl PUBLIC rensga_core ${RENSGA_SDL2_TARGET})
rensga_runtime_target(rensga_audio_sdl)
