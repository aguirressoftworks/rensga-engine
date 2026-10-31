# Core unit tests (null backends). Other roles add their own sub-directories with a CMakeLists.txt.
rensga_reflect_scan(tests_core RENSGA_CORE_TESTS "${CMAKE_CURRENT_SOURCE_DIR}/core")
add_executable(rensga_core_tests test_main.cpp ${RENSGA_CORE_TESTS})
target_link_libraries(rensga_core_tests PRIVATE rensga_api rensga_core rensga_reflect_json)
target_include_directories(rensga_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
rensga_runtime_target(rensga_core_tests)   # -fno-rtti: tests derive from core interfaces
add_test(NAME rensga_core_tests COMMAND rensga_core_tests)

file(GLOB children RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(child ${children})
  if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${child} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${child}/CMakeLists.txt)
    add_subdirectory(${child})
  endif()
endforeach()

# Source-tree rule checks (RULES.md, docs/rensga-engine/api-rules.md).
add_test(NAME check_api_rules COMMAND ${CMAKE_SOURCE_DIR}/tools/check_api_rules.sh)
if(NOT TEST check_editor_core)
  add_test(NAME check_editor_core COMMAND ${CMAKE_SOURCE_DIR}/tools/check_editor_core.sh)
endif()

# PSP GPU-renderer check (MVP black-screen bug class). Skipped (exit 77) when Docker / PPSSPPHeadless / xvfb are unavailable.
if(RENSGA_TARGET STREQUAL "linux-host")
  add_test(NAME psp_gpu_check COMMAND ${CMAKE_SOURCE_DIR}/tools/ci/psp_gpu_check.sh)
  set_tests_properties(psp_gpu_check PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900 LABELS "slow;psp")
endif()

# Codegen determinism and strip-guard checks (need host Python).
if(RENSGA_REFLECT_PYTHON)
  add_test(NAME reflect_scan_codegen COMMAND "${RENSGA_REFLECT_PYTHON}" ${CMAKE_SOURCE_DIR}/tools/reflect/test_reflect_scan.py
           ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/reflect_scan_tmp)
endif()

# F5.12: RENSGA_REQUIRES_FEATURE gives a compile error with a hint when the generated console_target.gen.h lacks the feature.
if(RENSGA_TARGET STREQUAL "linux-host" AND TARGET rensga-cooker)
  add_test(NAME feature_gate_check COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/core/feature_gate_check.sh ${CMAKE_CXX_COMPILER} $<TARGET_FILE:rensga-cooker> ${CMAKE_SOURCE_DIR})
endif()

# --- F5 A6 begin ---
# F5.12: scripts against the generated header of every profile; each gating error carries its hint. Also: the availability doc is not stale.
if(RENSGA_TARGET STREQUAL "linux-host" AND TARGET rensga-cooker)
  add_test(NAME api_profile COMMAND ${CMAKE_COMMAND} -E env RENSGA_COOKER=$<TARGET_FILE:rensga-cooker> RENSGA_CI_OUT=${CMAKE_CURRENT_BINARY_DIR}/api_profile_out
           ${CMAKE_SOURCE_DIR}/tools/ci/api_profile.sh)
endif()
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME api_availability_doc COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/docs/gen_api_availability.py --check)
endif()
# --- F5 A6 end ---

# --- F5 core3d begin ---
# Writes the pack3d fixture pack for tools/ci/determinism.sh 3dlit.
add_executable(pack3d_write determinism/pack3d_write.cpp)
target_link_libraries(pack3d_write PRIVATE rensga_api rensga_core rensga_reflect_json)
target_include_directories(pack3d_write PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
rensga_runtime_target(pack3d_write)
# --- F5 core3d end ---
