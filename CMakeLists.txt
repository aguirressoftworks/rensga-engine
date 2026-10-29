cmake_minimum_required(VERSION 3.24)
project(Rensga VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

# linux-host | psp | windows | android | ps2 | psx  (set by CMakePresets.json)
set(RENSGA_TARGET "linux-host" CACHE STRING "Build target slot")
option(RENSGA_BUILD_TESTS  "Build host unit tests"                 OFF)
option(RENSGA_BUILD_TOOLS  "Build cooker, pipeline and CLI"        OFF)
option(RENSGA_BUILD_EDITOR "Build the editor"                      OFF)
# Game project whose Assets/Scripts/*.cpp compile into the runtime executables.
set(RENSGA_PROJECT_DIR "${CMAKE_SOURCE_DIR}/templates/psp" CACHE PATH "Game project directory")
# Generated headers (console_target.gen.h) from the cooker land here.
set(RENSGA_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated" CACHE PATH "Generated headers directory")

if(NOT RENSGA_TARGET MATCHES "^(linux-host|psp)$")
  message(FATAL_ERROR "Target slot '${RENSGA_TARGET}' is declared but disabled (F1 and later).")
endif()

include(cmake/RensgaTargets.cmake)
include(CTest)  # defines BUILD_TESTING; tests are still gated by RENSGA_BUILD_TESTS

# Every planned directory is added when it has a CMakeLists.txt, so roles can land
# their modules without touching this file.
function(rensga_add_if_present dir)
  if(EXISTS "${CMAKE_SOURCE_DIR}/${dir}/CMakeLists.txt")
    add_subdirectory(${dir})
  endif()
endfunction()

rensga_add_if_present(third_party)

# Engine: always built (host sim and PSP).
rensga_add_if_present(engine/core)
rensga_add_if_present(engine/api)
if(RENSGA_TARGET STREQUAL "psp")
  rensga_add_if_present(engine/backends/platform/psp)
  rensga_add_if_present(engine/backends/render/gu)
endif()
rensga_add_if_present(engine/runtime)

# Host-only tools and editor.
if(RENSGA_BUILD_TOOLS OR RENSGA_BUILD_EDITOR)
  rensga_add_if_present(tools/cooker)
  rensga_add_if_present(tools/pipeline)
endif()
if(RENSGA_BUILD_EDITOR)
  rensga_add_if_present(editor/core)
  rensga_add_if_present(editor/targets/psp)
endif()
if(RENSGA_BUILD_TOOLS)
  # The CLI needs editor/core (TargetRegistry) and the PSP target module.
  if(NOT RENSGA_BUILD_EDITOR)
    rensga_add_if_present(editor/core)
    rensga_add_if_present(editor/targets/psp)
  endif()
  rensga_add_if_present(tools/cli)
endif()
if(RENSGA_BUILD_EDITOR)
  rensga_add_if_present(editor/app)
endif()

if(RENSGA_BUILD_TESTS)
  if(TARGET rensga_editor_core)
    rensga_add_if_present(editor/targets/dummy)  # test-only second target module (F1.04)
  endif()
  rensga_add_if_present(tests)
endif()
