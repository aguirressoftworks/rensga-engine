rensga_reflect_scan(tests_cooker RENSGA_COOKER_TEST_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/test_cooker.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test_primitives.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test_assets.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test_door.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test_psp.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test_f5_components.cpp)
# --- F5 A6 begin ---
list(APPEND RENSGA_COOKER_TEST_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/test_profile.cpp)
# --- F5 A6 end ---
add_executable(rensga_cooker_tests ../test_main.cpp ${RENSGA_COOKER_TEST_SRCS})
target_link_libraries(rensga_cooker_tests PRIVATE rensga_cooker_lib rensga_api)
target_include_directories(rensga_cooker_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. )
target_compile_definitions(rensga_cooker_tests PRIVATE RENSGA_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
                           RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp-3d")
rensga_runtime_target(rensga_cooker_tests)   # TestMover derives from Behaviour (-fno-rtti)
add_test(NAME rensga_cooker_tests COMMAND rensga_cooker_tests)

# CLI: the over-budget fixture must fail with the scene GUID in the error text.
add_test(NAME cooker_cli_over_budget
         COMMAND rensga-cooker cook --project ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/cook_over --force)
set_tests_properties(cooker_cli_over_budget PROPERTIES WILL_FAIL TRUE)
if(EXISTS ${CMAKE_SOURCE_DIR}/templates/psp-3d/Assets/Scenes/Main.scene)
  add_test(NAME cooker_cli_template
           COMMAND rensga-cooker cook --project ${CMAKE_SOURCE_DIR}/templates/psp-3d --force)
endif()

# F2.03: the 2D asset fixture cooks for a console profile and a platform target through the CLI.
foreach(tgt ps2 linux)
  add_test(NAME cooker_cli_assets2d_${tgt}
           COMMAND rensga-cooker cook --project ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/assets2d --target ${tgt} --force)
endforeach()

# F4.05: the same 2D sources on the PSP profile (stored sizes, swizzle, 16-bit formats).
add_test(NAME cooker_cli_assets2d_psp
         COMMAND rensga-cooker cook --project ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/assets2d_psp --target psp --force)

# F3.06: InputActions.json cooking (header-only parser uses exceptions, so it is a host-style test executable).
add_executable(rensga_input_cook_tests ../test_main.cpp test_input_cook.cpp)
target_link_libraries(rensga_input_cook_tests PRIVATE rensga_core nlohmann_json::nlohmann_json)
target_include_directories(rensga_input_cook_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_SOURCE_DIR}/tools/cooker/include)
rensga_host_target(rensga_input_cook_tests)
add_test(NAME input_cook COMMAND rensga_input_cook_tests)

# --- F5 A1 begin ---
add_executable(rensga_cook3d_tests ../test_main.cpp test_cook3d.cpp)
target_link_libraries(rensga_cook3d_tests PRIVATE rensga_cooker_lib rensga_api)
target_include_directories(rensga_cook3d_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_definitions(rensga_cook3d_tests PRIVATE RENSGA_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
                           RENSGA_ROOM_DIR="${CMAKE_SOURCE_DIR}/samples/psp3d_room"
                           RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp-3d")
rensga_runtime_target(rensga_cook3d_tests)
add_test(NAME cook3d COMMAND rensga_cook3d_tests)
add_test(NAME cooker_cli_psp3d_room_psp
         COMMAND rensga-cooker cook --project ${CMAKE_SOURCE_DIR}/samples/psp3d_room --target psp --force)
add_test(NAME cooker_cli_psp3d_room_linux
         COMMAND rensga-cooker cook --project ${CMAKE_SOURCE_DIR}/samples/psp3d_room --target linux --force)
# --- F5 A1 end ---
