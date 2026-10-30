# audsrv IOP module embedded into the ELF (bin2c ships with the pinned ps2dev image).
set(RENSGA_PS2_IRX_DIR "$ENV{PS2SDK}/iop/irx")
set(RENSGA_AUDSRV_C ${CMAKE_CURRENT_BINARY_DIR}/audsrv_irx.c)
add_custom_command(OUTPUT ${RENSGA_AUDSRV_C}
  COMMAND $ENV{PS2SDK}/bin/bin2c ${RENSGA_PS2_IRX_DIR}/audsrv.irx ${RENSGA_AUDSRV_C} audsrv_irx
  DEPENDS ${RENSGA_PS2_IRX_DIR}/audsrv.irx
  COMMENT "bin2c audsrv.irx")
add_library(rensga_audio_ps2 STATIC src/ps2_audio.cpp src/audsrv_blob.cpp ${RENSGA_AUDSRV_C})
set_source_files_properties(${RENSGA_AUDSRV_C} PROPERTIES HEADER_FILE_ONLY TRUE)
target_include_directories(rensga_audio_ps2 PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(rensga_audio_ps2 PUBLIC include)
target_link_libraries(rensga_audio_ps2 PUBLIC rensga_core audsrv patches)
rensga_runtime_target(rensga_audio_ps2)
