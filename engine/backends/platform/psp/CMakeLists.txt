add_library(rensga_platform_psp STATIC src/psp_platform.cpp src/psp_filesystem.cpp src/psp_log.cpp)
target_include_directories(rensga_platform_psp PUBLIC include)
target_link_libraries(rensga_platform_psp PUBLIC rensga_core)  # PSP system libs are linked once, in order, by the executable
rensga_runtime_target(rensga_platform_psp)
