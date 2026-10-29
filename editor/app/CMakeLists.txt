add_executable(rensga-editor main.cpp)
target_link_libraries(rensga-editor PRIVATE rensga_pipeline rensga_editor_core imgui)
rensga_link_whole_archive(rensga-editor PRIVATE rensga_target_psp)
rensga_host_target(rensga-editor)
