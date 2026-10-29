file(GLOB PSP_TARGET_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(rensga_target_psp STATIC ${PSP_TARGET_SOURCES})
target_link_libraries(rensga_target_psp PUBLIC rensga_editor_core)
rensga_host_target(rensga_target_psp)
