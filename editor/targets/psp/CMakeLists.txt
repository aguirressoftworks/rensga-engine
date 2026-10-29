# PSP target module (MVP-40): self-registers a TargetModule. No ImGui here; the CLI links it too.
file(GLOB PSP_TARGET_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(rensga_target_psp STATIC ${PSP_TARGET_SOURCES})
target_link_libraries(rensga_target_psp PUBLIC rensga_editor_core rensga_process)
target_compile_definitions(rensga_target_psp PRIVATE
  RENSGA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
  RENSGA_PSP_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
rensga_host_target(rensga_target_psp)
