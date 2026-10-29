# Header-only nlohmann/json (host tools only; never linked into the runtime).
add_library(nlohmann_json INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json)
target_include_directories(nlohmann_json SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json)

# Dear ImGui (docking) with SDL2 + OpenGL3 backends, editor only.
if(RENSGA_BUILD_EDITOR)
  find_package(SDL2 CONFIG QUIET)
  find_package(OpenGL QUIET)
  add_library(imgui STATIC
    imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp imgui/imgui_stdlib.cpp
    imgui/backends/imgui_impl_sdl2.cpp imgui/backends/imgui_impl_opengl3.cpp)
  target_include_directories(imgui SYSTEM PUBLIC imgui imgui/backends)
  if(TARGET SDL2::SDL2)
    target_link_libraries(imgui PUBLIC SDL2::SDL2)
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2PC REQUIRED IMPORTED_TARGET sdl2)
    target_link_libraries(imgui PUBLIC PkgConfig::SDL2PC)
  endif()
  target_link_libraries(imgui PUBLIC ${CMAKE_DL_LIBS})
  if(TARGET OpenGL::GL)
    target_link_libraries(imgui PUBLIC OpenGL::GL)
  endif()
endif()
