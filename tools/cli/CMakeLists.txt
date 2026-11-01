add_executable(rensga main.cpp)
target_link_libraries(rensga PRIVATE rensga_pipeline rensga_modules_cmd)
# Target modules self-register; keep their static registrars alive.
rensga_link_whole_archive(rensga PRIVATE rensga_target_psp)
# F6.05 (hook register row F6.05): the cook loads scenes through reflection, so project script types must be registered in-process. The CLI
# no longer links the scripts of one build-time project: like the editor it loads the project's own script module
# (<project>/Library/ScriptAssemblies, built on demand by editor/core game_module), so `rensga cook|build|play <any project>` works.
# The module links no engine code; the engine's symbols resolve from this executable (whole archives + ENABLE_EXPORTS), as in the editor.
rensga_link_whole_archive(rensga PRIVATE rensga_core)
rensga_link_whole_archive(rensga PRIVATE rensga_api)
set_target_properties(rensga PROPERTIES ENABLE_EXPORTS ON)
rensga_host_target(rensga)
