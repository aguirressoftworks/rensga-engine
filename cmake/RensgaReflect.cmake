# Reflection codegen (F1.05): rensga_reflect_scan(<group> <out-var> <files-or-dirs...>)
# Runs tools/reflect/reflect_scan.py at configure time and returns the sources to compile (generated
# *.reflect.gen.cpp replace the scanned .cpp files; generated tables for scanned headers are added).
# The cross toolchain images have no Python, so cross builds read the manifest written on the host by
# tools/reflect/prescan.sh into RENSGA_REFLECT_DIR (the PSP target module and tools/ci/*.sh do this).
set(RENSGA_REFLECT_DIR "${CMAKE_BINARY_DIR}/reflect" CACHE PATH "Reflection codegen output")
find_program(RENSGA_REFLECT_PYTHON NAMES python3 python NO_CMAKE_FIND_ROOT_PATH)

function(rensga_reflect_scan group outvar)
  set(inputs ${ARGN})
  set(deps "")
  foreach(in ${inputs})
    if(IS_DIRECTORY "${in}")
      file(GLOB_RECURSE found CONFIGURE_DEPENDS "${in}/*.h" "${in}/*.hpp" "${in}/*.cpp")
      list(APPEND deps ${found})
    elseif(EXISTS "${in}")
      list(APPEND deps "${in}")
    endif()
  endforeach()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${deps})
  if(RENSGA_REFLECT_PYTHON)
    execute_process(COMMAND "${RENSGA_REFLECT_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/reflect/reflect_scan.py"
                    --out "${RENSGA_REFLECT_DIR}" --group ${group} ${inputs}
                    RESULT_VARIABLE rc ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "reflection codegen failed:\n${err}")
    endif()
  elseif(NOT EXISTS "${RENSGA_REFLECT_DIR}/${group}.cmake")
    message(FATAL_ERROR "No Python on this build host and no ${RENSGA_REFLECT_DIR}/${group}.cmake. "
                        "Run tools/reflect/prescan.sh <reflect-dir> <project> on the host first.")
  endif()
  include("${RENSGA_REFLECT_DIR}/${group}.cmake")
  string(TOUPPER "${group}" G)
  set(${outvar} ${RENSGA_REFLECT_${G}_SOURCES} PARENT_SCOPE)
endfunction()

# Runtime (cross) builds drop editor-only metadata (headers, tooltips, ranges, required components).
# The linux-host build keeps it: the editor, cooker and tests share these libraries.
option(RENSGA_STRIP_EDITOR_METADATA "Strip editor-only reflection metadata" OFF)
if(NOT RENSGA_TARGET STREQUAL "linux-host")
  set(RENSGA_STRIP_EDITOR_METADATA ON CACHE BOOL "" FORCE)
endif()
if(RENSGA_STRIP_EDITOR_METADATA)
  add_compile_definitions(RENSGA_STRIP_EDITOR_METADATA)
endif()
