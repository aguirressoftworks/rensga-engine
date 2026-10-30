add_library(rensga_platform_psp STATIC src/psp_platform.cpp src/psp_filesystem.cpp src/psp_log.cpp)
target_include_directories(rensga_platform_psp PUBLIC include)
target_link_libraries(rensga_platform_psp PUBLIC rensga_core)  # PSP system libs are linked once, in order, by the executable
# Release (PRX/EBOOT) builds read data from ms0:/PSP/GAME/<id>/; dev builds use host0:/. The editor passes
# -DRENSGA_PSP_PRX=ON for release (a cache variable, so it is already set here). PUBLIC so main_psp sees it too.
if(RENSGA_PSP_PRX)
  target_compile_definitions(rensga_platform_psp PUBLIC RENSGA_RELEASE)
endif()
set(RENSGA_PSP_GAME_ID "RENSGA" CACHE STRING "PSP release folder name under ms0:/PSP/GAME/")
target_compile_definitions(rensga_platform_psp PUBLIC RENSGA_PSP_GAME_ID="${RENSGA_PSP_GAME_ID}")
rensga_runtime_target(rensga_platform_psp)
