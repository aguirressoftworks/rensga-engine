set(RENSGA_PALETTE_FILE ${CMAKE_SOURCE_DIR}/docs/rensga-engine/brand/palette.json)
file(READ ${RENSGA_PALETTE_FILE} RENSGA_PALETTE_JSON)
configure_file(brand_palette.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/brand_palette.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RENSGA_PALETTE_FILE})

# F6 E0: panels register themselves (panel_registry.h), so every panel/drawer/render source in these folders is picked up
# without editing this file or main.cpp. CONFIGURE_DEPENDS re-runs the glob when a file is added.
file(GLOB RENSGA_EDITOR_PANEL_SRCS CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/panels/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/panels/drawers/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/render/*.cpp)
add_executable(rensga-editor
  main.cpp theme.cpp screenshot.cpp panel_registry.cpp ${RENSGA_EDITOR_PANEL_SRCS}
  ${CMAKE_CURRENT_BINARY_DIR}/brand_palette.cpp)
target_include_directories(rensga-editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(rensga-editor SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
target_compile_definitions(rensga-editor PRIVATE RENSGA_FONT_DIR="${CMAKE_SOURCE_DIR}/third_party/fonts"
  RENSGA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" RENSGA_VERSION="${PROJECT_VERSION}")
# The build tree this editor came from: script changes rebuild it (MVP-57).
target_compile_definitions(rensga-editor PRIVATE
  RENSGA_EDITOR_BUILD_DIR="${CMAKE_BINARY_DIR}" RENSGA_EDITOR_CMAKE="${CMAKE_COMMAND}"
  RENSGA_EDITOR_PROJECT_DIR="${RENSGA_PROJECT_DIR}")
target_link_libraries(rensga-editor PRIVATE rensga_pipeline rensga_editor_core rensga_core rensga_render_gl rensga_platform_sdl imgui imguizmo imnodes)
rensga_link_whole_archive(rensga-editor PRIVATE rensga_target_psp)
# Project scripts are NOT linked into the editor (F3.03): they build into <project>/Library/ScriptAssemblies and
# load at runtime (editor/core game_module). The module links no engine code, so the editor exports the whole
# engine: every rensga_core / rensga_api object is kept and the executable's symbols are visible to dlopen'd code.
rensga_link_whole_archive(rensga-editor PRIVATE rensga_core)
rensga_link_whole_archive(rensga-editor PRIVATE rensga_api)
set_target_properties(rensga-editor PROPERTIES ENABLE_EXPORTS ON)
if(WIN32)  # not built or tested yet: export everything and let the module link the import library
  target_link_options(rensga-editor PRIVATE -Wl,--export-all-symbols -Wl,--out-implib,librensga-editor.dll.a)
  # TODO(Windows): pass -DRENSGA_EDITOR_IMPORT_LIB=<this import lib> to the module configure in game_module.cpp.
endif()
rensga_host_target(rensga-editor)

# --- F5 dock+debug begin ---
# F5.23 docked Emulator panel (X11: XReparentWindow, Win32: SetParent, floating fallback) and F5.10 Profiler panel.
target_sources(rensga-editor PRIVATE platform/x11_embed.cpp platform/win32_embed.cpp)
if(NOT WIN32)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(RENSGA_EDITOR_X11 QUIET IMPORTED_TARGET x11)
  endif()
  if(TARGET PkgConfig::RENSGA_EDITOR_X11)
    target_link_libraries(rensga-editor PRIVATE PkgConfig::RENSGA_EDITOR_X11)
    target_compile_definitions(rensga-editor PRIVATE RENSGA_HAVE_X11=1)
  endif()
endif()
# --- F5 dock+debug end ---

add_test(NAME editor_core_console_agnostic
         COMMAND ${CMAKE_SOURCE_DIR}/tools/check_editor_core.sh)

# F3.03: script build in the real editor (error -> console file:line, no freeze, open in editor, fix -> reload). Skips (77) without xvfb.
add_test(NAME editor_script_build_ui
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/editor/script_build_ui.sh $<TARGET_FILE:rensga-editor>
                 ${CMAKE_SOURCE_DIR}/templates/psp-3d ${CMAKE_BINARY_DIR}/editor_script_build_console.png)
set_tests_properties(editor_script_build_ui PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 1200 LABELS "slow;gl"
                     ENVIRONMENT "RENSGA_JOBS=2")

# --- F5 B1 begin ---
# F5.04 / F5.09: Window > Emulators, the "emulator missing" offer, Build and Run.
# (panels/emulator_manager.cpp is picked up by the panel glob above)
# --- F5 B1 end ---

# --- F6 d3 begin ---
# F6.07: editor extensions host (builds/loads <project>/Editor, windows, menus, custom inspectors). The panel registers itself.
target_sources(rensga-editor PRIVATE extensions/ext_host.cpp)
# --- F6 d3 end ---
