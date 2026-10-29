# Test-only target module (F1.04): packages a zip, nothing else. Proves a second console registers
# without touching editor/core. Not linked into the editor or the CLI.
add_library(rensga_target_dummy STATIC src/dummy_target.cpp)
target_link_libraries(rensga_target_dummy PUBLIC rensga_editor_core)
rensga_host_target(rensga_target_dummy)
