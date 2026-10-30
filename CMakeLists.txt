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

if(RENSGA_TARGET STREQUAL "psx")
  message(FATAL_ERROR "Target slot 'psx' is declared but disabled until F9.")
elseif(NOT RENSGA_TARGET MATCHES "^(linux-host|psp|windows|android|ps2)$")
  message(FATAL_ERROR "Unknown target slot '${RENSGA_TARGET}'.")
endif()

# Determinism (RULES 1.9): no fused multiply-add contraction and no fast-math on any preset (GCC and Clang).
add_compile_options(-ffp-contract=off)
foreach(_fl CMAKE_C_FLAGS CMAKE_CXX_FLAGS CMAKE_C_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELEASE CMAKE_C_FLAGS_DEBUG CMAKE_CXX_FLAGS_DEBUG)
  if("${${_fl}}" MATCHES "-ffast-math|-Ofast")
    message(FATAL_ERROR "${_fl} contains fast-math/-Ofast; the engine must be deterministic.")
  endif()
endforeach()

# SDL2 for the desktop / Android runtime (platform/sdl, render/gl). Pinned: 2.30.8 (Windows devel zip and
# Android source are fetched by sha256 in tools/docker/{windows,android}/Dockerfile).
set(RENSGA_SDL2_TARGET "")
if(RENSGA_TARGET STREQUAL "android")
  set(SDL2_ROOT "$ENV{SDL2_ROOT}" CACHE PATH "SDL2 source tree (Android image: /opt/SDL2-2.30.8)")
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  add_subdirectory(${SDL2_ROOT} ${CMAKE_BINARY_DIR}/sdl2 EXCLUDE_FROM_ALL)
  set(RENSGA_SDL2_TARGET SDL2::SDL2)
  if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 ALIAS SDL2)
  endif()
elseif(RENSGA_TARGET STREQUAL "windows")
  find_package(SDL2 CONFIG REQUIRED PATHS "$ENV{SDL2_MINGW_ROOT}" NO_DEFAULT_PATH)
  set(RENSGA_SDL2_TARGET SDL2::SDL2)
elseif(RENSGA_TARGET STREQUAL "linux-host")
  find_package(SDL2 CONFIG QUIET)
  if(TARGET SDL2::SDL2)
    set(RENSGA_SDL2_TARGET SDL2::SDL2)
  endif()
endif()

include(cmake/RensgaTargets.cmake)
include(cmake/RensgaReflect.cmake)
# Reflection tables: engine components and the project scripts (used by core, runtime, cooker, tests).
rensga_reflect_scan(engine RENSGA_REFLECT_ENGINE_SRCS "${CMAKE_SOURCE_DIR}/engine/core/include/rensga/core/components.h"
                    "${CMAKE_SOURCE_DIR}/engine/core/include/rensga/core/gameplay.h")
rensga_reflect_scan(project RENSGA_REFLECT_PROJECT_SRCS "${RENSGA_PROJECT_DIR}/Assets/Scripts")
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
if(RENSGA_TARGET MATCHES "^(windows|android)$" OR (RENSGA_TARGET STREQUAL "linux-host" AND RENSGA_SDL2_TARGET))
  rensga_add_if_present(engine/backends/platform/sdl)
  rensga_add_if_present(engine/backends/render/gl)
  rensga_add_if_present(engine/backends/audio/sdl)
endif()
if(RENSGA_TARGET STREQUAL "ps2")
  rensga_add_if_present(engine/backends/platform/ps2)
  rensga_add_if_present(engine/backends/render/gs)
  rensga_add_if_present(engine/backends/audio/ps2)
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
