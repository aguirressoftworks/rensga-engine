add_executable(rensga main.cpp)
target_link_libraries(rensga PRIVATE rensga_pipeline)
# Target modules self-register; keep their static registrars alive.
rensga_link_whole_archive(rensga PRIVATE rensga_target_psp)
rensga_host_target(rensga)
