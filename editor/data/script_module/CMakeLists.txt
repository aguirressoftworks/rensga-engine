# Game script module (F3.03): <project>/Assets/Scripts/*.{h,cpp} -> librensga_game.so / rensga_game.dll.
# Configured by the editor (editor/core game_module.cpp), never by the main build:
#   cmake -S editor/data/script_module -B <project>/Library/ScriptBuild -DRENSGA_PROJECT_DIR=<project>
#         -DRENSGA_SOURCE_DIR=<engine checkout> -DRENSGA_SCRIPT_OUTPUT_DIR=<project>/Library/ScriptAssemblies
#
# The library links no engine code. Engine symbols (TypeRegistry, World, Time, ...) resolve from the editor
# executable at load time: the editor is linked with ENABLE_EXPORTS (-rdynamic) and the engine archives whole.
# On Windows the editor produces an import library (RENSGA_EDITOR_IMPORT_LIB) that the module links against.
# Same sources, same flags as the static builds (no exceptions, no RTTI, no FMA contraction), so a game's code
# behaves the same in the editor, the player and on the console.
cmake_minimum_required(VERSION 3.24)
project(RensgaGameModule LANGUAGES CXX)

foreach(var RENSGA_PROJECT_DIR RENSGA_SOURCE_DIR RENSGA_SCRIPT_OUTPUT_DIR)
  if(NOT ${var})
    message(FATAL_ERROR "${var} is required")
  endif()
endforeach()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

find_program(RENSGA_REFLECT_PYTHON NAMES python3 python)
if(NOT RENSGA_REFLECT_PYTHON)
  message(FATAL_ERROR "Python 3 is needed for the reflection codegen (tools/reflect/reflect_scan.py)")
endif()

set(RENSGA_REFLECT_DIR "${CMAKE_BINARY_DIR}/reflect")
set(scripts_dir "${RENSGA_PROJECT_DIR}/Assets/Scripts")
file(GLOB_RECURSE script_files CONFIGURE_DEPENDS "${scripts_dir}/*.h" "${scripts_dir}/*.hpp" "${scripts_dir}/*.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${script_files}
             "${RENSGA_SOURCE_DIR}/tools/reflect/reflect_scan.py")
execute_process(COMMAND "${RENSGA_REFLECT_PYTHON}" "${RENSGA_SOURCE_DIR}/tools/reflect/reflect_scan.py"
                --out "${RENSGA_REFLECT_DIR}" --group project "${scripts_dir}"
                RESULT_VARIABLE rc ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "reflection codegen failed:\n${err}")
endif()
include("${RENSGA_REFLECT_DIR}/project.cmake")

file(WRITE "${CMAKE_BINARY_DIR}/module_stub.cpp" "// keeps an empty script folder a valid library\nnamespace rensga_game_module_stub {}\n")
add_library(rensga_game SHARED ${RENSGA_REFLECT_PROJECT_SOURCES} "${CMAKE_BINARY_DIR}/module_stub.cpp")
target_include_directories(rensga_game PRIVATE
  "${RENSGA_SOURCE_DIR}/engine/core/include" "${RENSGA_SOURCE_DIR}/engine/api/include")
target_compile_options(rensga_game PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti -ffp-contract=off)
if(NOT WIN32)
  # Inline static data (TypeBuilder<T>::info) must not become STB_GNU_UNIQUE: unique symbols pin the old copy in
  # memory after dlclose, and a reloaded module would bind to the stale definitions.
  target_compile_options(rensga_game PRIVATE -fno-gnu-unique)
endif()
if(WIN32 AND RENSGA_EDITOR_IMPORT_LIB)
  target_link_libraries(rensga_game PRIVATE "${RENSGA_EDITOR_IMPORT_LIB}")
endif()
set_target_properties(rensga_game PROPERTIES
  OUTPUT_NAME rensga_game
  LIBRARY_OUTPUT_DIRECTORY "${RENSGA_SCRIPT_OUTPUT_DIR}"
  RUNTIME_OUTPUT_DIRECTORY "${RENSGA_SCRIPT_OUTPUT_DIR}")
