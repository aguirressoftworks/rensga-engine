add_executable(rensga main.cpp)
target_link_libraries(rensga PRIVATE rensga_pipeline rensga_modules_cmd)
# Target modules self-register; keep their static registrars alive.
rensga_link_whole_archive(rensga PRIVATE rensga_target_psp)
# The cook loads scenes through reflection, so project script types must be registered in-process.
# MVP limitation: only the scripts of RENSGA_PROJECT_DIR (build-time) are known; see TASKLIST deviations.
if(TARGET rensga_cooker_scripts)
  target_link_libraries(rensga PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,rensga_cooker_scripts>")
endif()
rensga_host_target(rensga)
