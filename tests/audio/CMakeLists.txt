# Engine 4 audio tests: mixer unit/stress tests (null file system, no device) and, where SDL2 exists,
# a run through the SDL backend with the "disk" audio driver (writes the device output to a file).
add_executable(rensga_audio_tests ${CMAKE_SOURCE_DIR}/tests/test_main.cpp test_audio.cpp)
target_link_libraries(rensga_audio_tests PRIVATE rensga_api rensga_core)
target_include_directories(rensga_audio_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests)
rensga_runtime_target(rensga_audio_tests)
add_test(NAME rensga_audio_tests COMMAND rensga_audio_tests)

if(TARGET rensga_audio_sdl)
  add_executable(rensga_audio_sdl_check sdl_audio_check.cpp)
  target_link_libraries(rensga_audio_sdl_check PRIVATE rensga_audio_sdl rensga_core)
  rensga_runtime_target(rensga_audio_sdl_check)
  add_test(NAME audio_sdl_disk COMMAND rensga_audio_sdl_check ${CMAKE_CURRENT_BINARY_DIR}/sdl_out.raw)
  set_tests_properties(audio_sdl_disk PROPERTIES TIMEOUT 60 PASS_REGULAR_EXPRESSION "rensga: audio-sdl-check ok")
endif()
