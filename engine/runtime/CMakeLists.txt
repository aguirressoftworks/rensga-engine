# Runtime executables. Project scripts (<project>/Assets/Scripts/*.cpp) are compiled as SOURCES of the
# executables (not a static lib) so their static REFLECT registrars survive linking.
file(GLOB RENSGA_PROJECT_SCRIPTS CONFIGURE_DEPENDS "${RENSGA_PROJECT_DIR}/Assets/Scripts/*.cpp")

# Engine 0 hello: one main on linux-host (needs SDL2), windows, android and ps2.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_hello.cpp AND NOT RENSGA_TARGET STREQUAL "psp")
  if(RENSGA_TARGET STREQUAL "android")
    add_library(rensga_hello SHARED main_hello.cpp)  # SDLActivity loads libmain.so
    set_target_properties(rensga_hello PROPERTIES OUTPUT_NAME main)
    target_link_libraries(rensga_hello PRIVATE log)
  elseif(RENSGA_TARGET STREQUAL "ps2")
    add_executable(rensga_hello main_hello.cpp)
    target_compile_definitions(rensga_hello PRIVATE RENSGA_PLATFORM_PS2)
    target_link_libraries(rensga_hello PRIVATE rensga_platform_ps2 rensga_render_gs gskit dmakit c)
    set_target_properties(rensga_hello PROPERTIES SUFFIX ".elf")
  elseif(TARGET rensga_platform_sdl)
    add_executable(rensga_hello main_hello.cpp)
    if(RENSGA_TARGET STREQUAL "windows")
      target_link_options(rensga_hello PRIVATE -static-libgcc -static-libstdc++)
    endif()
  endif()
  if(TARGET rensga_hello)
    target_link_libraries(rensga_hello PRIVATE rensga_core)
    if(TARGET rensga_platform_sdl)
      target_link_libraries(rensga_hello PRIVATE rensga_platform_sdl rensga_render_gl)
    endif()
    rensga_runtime_target(rensga_hello)
  endif()
endif()

if(RENSGA_TARGET STREQUAL "psp")
  # PSP ELF. Backends come from engine/backends/*; the PSP role owns main_psp.cpp.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_psp.cpp)
    add_executable(rensga_game main_psp.cpp ${RENSGA_PROJECT_SCRIPTS})
    target_link_libraries(rensga_game PRIVATE rensga_api rensga_core)
    foreach(backend rensga_platform_psp rensga_render_gu)
      if(TARGET ${backend})
        target_link_libraries(rensga_game PRIVATE ${backend})
      endif()
    endforeach()
    # PSP system libraries: link each once, in pspsdk dependency order (users before providers).
    # If a module's import stubs end up split (e.g. pspdisplay pulled in twice from different
    # positions), psp-fixup-imports assigns the wrong NIDs and calls silently hit other functions.
    target_link_libraries(rensga_game PRIVATE pspgum pspgu pspge pspdisplay pspctrl psppower pspdebug pspsdk m)
    target_include_directories(rensga_game PRIVATE ${RENSGA_GENERATED_DIR})
    rensga_runtime_target(rensga_game)
    set(RENSGA_EXIT_AFTER_FRAMES 0 CACHE STRING "PSP: exit the game after N frames (screenshots/CI); 0 = never")
    if(RENSGA_EXIT_AFTER_FRAMES GREATER 0)
      target_compile_definitions(rensga_game PRIVATE RENSGA_EXIT_AFTER_FRAMES=${RENSGA_EXIT_AFTER_FRAMES})
    endif()
    set_target_properties(rensga_game PROPERTIES OUTPUT_NAME rensga_game SUFFIX ".elf")
    # PSP stubs must be fixed up or the imports stay unresolved (PPSSPP and hardware both need it).
    target_link_options(rensga_game PRIVATE -Wl,-q)
    add_custom_command(TARGET rensga_game POST_BUILD
      COMMAND "$ENV{PSPDEV}/bin/psp-fixup-imports" "$<TARGET_FILE:rensga_game>"
      COMMENT "psp-fixup-imports rensga_game")
    # Release: link as a PRX module (packaged into EBOOT.PBP by the pipeline's Package stage).
    option(RENSGA_PSP_PRX "Link rensga_game as a PRX and generate rensga_game.elf.prx" OFF)
    if(RENSGA_PSP_PRX)
      target_link_options(rensga_game PRIVATE
        -specs=$ENV{PSPDEV}/psp/sdk/lib/prxspecs
        -Wl,-T$ENV{PSPDEV}/psp/sdk/lib/linkfile.prx
        $ENV{PSPDEV}/psp/sdk/lib/prxexports.o)
      add_custom_command(TARGET rensga_game POST_BUILD
        COMMAND "$ENV{PSPDEV}/bin/psp-prxgen" "$<TARGET_FILE:rensga_game>" "$<TARGET_FILE:rensga_game>.prx"
        COMMENT "psp-prxgen rensga_game")
    endif()
  endif()
elseif(RENSGA_TARGET STREQUAL "linux-host")
  # Linux-host simulation: null backends + stdio file system, no window.
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/main_headless.cpp)
    add_executable(rensga_headless main_headless.cpp ${RENSGA_PROJECT_SCRIPTS})
    target_link_libraries(rensga_headless PRIVATE rensga_api rensga_core)
    target_include_directories(rensga_headless PRIVATE ${RENSGA_GENERATED_DIR})
    rensga_runtime_target(rensga_headless)
  endif()
endif()
