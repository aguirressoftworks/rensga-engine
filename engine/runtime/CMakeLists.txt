# Runtime executables. Project scripts (<project>/Assets/Scripts/*.cpp) are compiled as SOURCES of the
# executables (not a static lib) so their static REFLECT registrars survive linking.
file(GLOB RENSGA_PROJECT_SCRIPTS CONFIGURE_DEPENDS "${RENSGA_PROJECT_DIR}/Assets/Scripts/*.cpp")

if(RENSGA_TARGET STREQUAL "psp")
  # PSP ELF. Backends come from engine/backends/*; the PSP role owns main_psp.cpp.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_psp.cpp)
    add_executable(rensga_game main_psp.cpp ${RENSGA_PROJECT_SCRIPTS})
    target_link_libraries(rensga_game PRIVATE rensga_api rensga_core)
    foreach(backend rensga_platform_psp rensga_render_gu)
      if(TARGET ${backend})
        target_link_libraries(rensga_game PRIVATE ${backend})
      endif()
    endforeach()
    target_link_libraries(rensga_game PRIVATE pspdebug pspdisplay pspge pspgu pspctrl psppower pspsdk m)
    target_include_directories(rensga_game PRIVATE ${RENSGA_GENERATED_DIR})
    rensga_runtime_target(rensga_game)
    set_target_properties(rensga_game PROPERTIES OUTPUT_NAME rensga_game)
  endif()
else()
  # Linux-host simulation: null backends + stdio file system, no window.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_headless.cpp)
    add_executable(rensga_headless main_headless.cpp ${RENSGA_PROJECT_SCRIPTS})
    target_link_libraries(rensga_headless PRIVATE rensga_api rensga_core)
    target_include_directories(rensga_headless PRIVATE ${RENSGA_GENERATED_DIR})
    rensga_runtime_target(rensga_headless)
  endif()
endif()
