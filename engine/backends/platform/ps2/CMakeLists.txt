add_library(rensga_platform_ps2 STATIC src/ps2_platform.cpp)
target_include_directories(rensga_platform_ps2 PUBLIC include)
target_link_libraries(rensga_platform_ps2 PUBLIC rensga_core pad)  # libpad: SIO2MAN/PADMAN are loaded from rom0: at runtime
rensga_runtime_target(rensga_platform_ps2)
