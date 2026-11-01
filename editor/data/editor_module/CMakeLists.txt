# Editor extension module (F6.07): <project>/Editor/**/*.{h,cpp} -> librensga_editor_ext.so / rensga_editor_ext.dll.
# Configured by the editor (editor/core extension.cpp), never by the main build and never by a player or console build:
#   cmake -S editor/data/editor_module -B <project>/Library/EditorBuild -DRENSGA_PROJECT_DIR=<project>
#         -DRENSGA_SOURCE_DIR=<engine checkout> -DRENSGA_EXT_OUTPUT_DIR=<project>/Library/EditorAssemblies
#
# Like the game module the library links nothing: the editor API (rensga/editor/extension.h), ImGui and the engine resolve from the
# editor executable at load time (ENABLE_EXPORTS). Unlike the game module it is editor code, so exceptions and RTTI stay on, as in
# the editor itself, and there is no reflection codegen: editor code reads components through ComponentView by field name.
cmake_minimum_required(VERSION 3.24)
project(RensgaEditorModule LANGUAGES CXX)

foreach(var RENSGA_PROJECT_DIR RENSGA_SOURCE_DIR RENSGA_EXT_OUTPUT_DIR)
  if(NOT ${var})
    message(FATAL_ERROR "${var} is required")
  endif()
endforeach()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(editor_dir "${RENSGA_PROJECT_DIR}/Editor")
file(GLOB_RECURSE editor_files CONFIGURE_DEPENDS "${editor_dir}/*.h" "${editor_dir}/*.hpp" "${editor_dir}/*.cpp")
set(editor_sources "")
foreach(f ${editor_files})
  if(f MATCHES "\\.cpp$")
    list(APPEND editor_sources "${f}")
  endif()
endforeach()
file(WRITE "${CMAKE_BINARY_DIR}/module_stub.cpp" "// keeps an Editor/ folder with only headers a valid library\nnamespace rensga_editor_module_stub {}\n")
add_library(rensga_editor_ext SHARED ${editor_sources} "${CMAKE_BINARY_DIR}/module_stub.cpp")
target_include_directories(rensga_editor_ext PRIVATE "${editor_dir}")
target_include_directories(rensga_editor_ext SYSTEM PRIVATE
  "${RENSGA_SOURCE_DIR}/editor/core/include" "${RENSGA_SOURCE_DIR}/engine/core/include" "${RENSGA_SOURCE_DIR}/engine/api/include"
  "${RENSGA_SOURCE_DIR}/tools/cooker/include" "${RENSGA_SOURCE_DIR}/tools/pipeline/include" "${RENSGA_SOURCE_DIR}/tools/modules/include"
  "${RENSGA_SOURCE_DIR}/third_party/imgui" "${RENSGA_SOURCE_DIR}/third_party/nlohmann_json")
target_compile_options(rensga_editor_ext PRIVATE -Wall -Wextra -ffp-contract=off)
if(NOT WIN32)
  # Inline statics must not become STB_GNU_UNIQUE: a unique symbol pins the old copy in memory after dlclose.
  target_compile_options(rensga_editor_ext PRIVATE -fno-gnu-unique)
endif()
if(WIN32 AND RENSGA_EDITOR_IMPORT_LIB)
  target_link_libraries(rensga_editor_ext PRIVATE "${RENSGA_EDITOR_IMPORT_LIB}")
endif()
set_target_properties(rensga_editor_ext PROPERTIES
  OUTPUT_NAME rensga_editor_ext
  LIBRARY_OUTPUT_DIRECTORY "${RENSGA_EXT_OUTPUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY "${RENSGA_EXT_OUTPUT_DIR}")
