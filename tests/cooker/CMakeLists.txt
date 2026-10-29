rensga_reflect_scan(tests_cooker RENSGA_COOKER_TEST_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/test_cooker.cpp ${CMAKE_CURRENT_SOURCE_DIR}/test_primitives.cpp)
add_executable(rensga_cooker_tests ../test_main.cpp ${RENSGA_COOKER_TEST_SRCS})
target_link_libraries(rensga_cooker_tests PRIVATE rensga_cooker_lib rensga_api)
target_include_directories(rensga_cooker_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. )
target_compile_definitions(rensga_cooker_tests PRIVATE RENSGA_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../fixtures"
                           RENSGA_TEMPLATE_DIR="${CMAKE_SOURCE_DIR}/templates/psp")
rensga_runtime_target(rensga_cooker_tests)   # TestMover derives from Behaviour (-fno-rtti)
add_test(NAME rensga_cooker_tests COMMAND rensga_cooker_tests)

# CLI: the over-budget fixture must fail with the scene GUID in the error text.
add_test(NAME cooker_cli_over_budget
         COMMAND rensga-cooker cook --project ${CMAKE_CURRENT_SOURCE_DIR}/../fixtures/cook_over --force)
set_tests_properties(cooker_cli_over_budget PROPERTIES WILL_FAIL TRUE)
if(EXISTS ${CMAKE_SOURCE_DIR}/templates/psp/Assets/Scenes/Main.scene)
  add_test(NAME cooker_cli_template
           COMMAND rensga-cooker cook --project ${CMAKE_SOURCE_DIR}/templates/psp --force)
endif()
