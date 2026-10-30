# ISO 9660 writer/reader library and the rensga-iso CLI (replaces xorriso for PS2 images). Host only.
add_library(rensga_iso STATIC src/iso.cpp)
target_include_directories(rensga_iso PUBLIC include)
rensga_host_target(rensga_iso)

add_executable(rensga-iso src/main.cpp)
target_link_libraries(rensga-iso PRIVATE rensga_iso)
rensga_host_target(rensga-iso)
