set(RENSGA_PALETTE_FILE ${CMAKE_SOURCE_DIR}/docs/rensga-engine/brand/palette.json)
file(READ ${RENSGA_PALETTE_FILE} RENSGA_PALETTE_JSON)
configure_file(brand_palette.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/brand_palette.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RENSGA_PALETTE_FILE})

add_executable(rensga-editor
  main.cpp theme.cpp screenshot.cpp
  panels/layout.cpp panels/menus.cpp panels/toolbar.cpp panels/hierarchy.cpp panels/inspector.cpp
  panels/console.cpp panels/project_panel.cpp panels/scene_view.cpp panels/game_view.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/brand_palette.cpp)
target_include_directories(rensga-editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(rensga-editor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_compile_definitions(rensga-editor PRIVATE RENSGA_FONT_DIR="${CMAKE_SOURCE_DIR}/third_party/fonts")
target_link_libraries(rensga-editor PRIVATE rensga_pipeline rensga_editor_core rensga_core imgui)
rensga_link_whole_archive(rensga-editor PRIVATE rensga_target_psp)
# Project script types must be registered in-process so the cook can read scenes (MVP limitation, see TASKLIST deviations).
if(TARGET rensga_cooker_scripts)
  target_link_libraries(rensga-editor PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,rensga_cooker_scripts>")
endif()
rensga_host_target(rensga-editor)

add_test(NAME editor_core_console_agnostic
         COMMAND ${CMAKE_SOURCE_DIR}/tools/check_editor_core.sh)
