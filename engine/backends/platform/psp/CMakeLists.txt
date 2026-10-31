add_library(rensga_platform_psp STATIC src/psp_platform.cpp src/psp_filesystem.cpp src/psp_log.cpp)
target_include_directories(rensga_platform_psp PUBLIC include)
target_link_libraries(rensga_platform_psp PUBLIC rensga_core)  # PSP system libs are linked once, in order, by the executable
# Release (PRX/EBOOT) builds read data from ms0:/PSP/GAME/<id>/; dev builds use host0:/. The editor passes
# -DRENSGA_PSP_PRX=ON for release (a cache variable, so it is already set here). PUBLIC so main_psp sees it too.
if(RENSGA_PSP_PRX)
  target_compile_definitions(rensga_platform_psp PUBLIC RENSGA_RELEASE)
endif()
set(RENSGA_PSP_GAME_ID "RENSGA" CACHE STRING "PSP release folder name under ms0:/PSP/GAME/")
target_compile_definitions(rensga_platform_psp PUBLIC RENSGA_PSP_GAME_ID="${RENSGA_PSP_GAME_ID}")
rensga_runtime_target(rensga_platform_psp)

# F4.03: SDL2 (static, from the psp-build module) for the game controller and SdlAudio. SDL_MAIN_HANDLED, no SDL2main, no SDL video.
# $ENV{PSPDEV} is the toolchain root (set by tools/modules/toolchain_env.sh); the headers are in psp/include/SDL2.
target_link_libraries(rensga_platform_psp PUBLIC ${RENSGA_SDL2_TARGET})  # headers + SDL_MAIN_HANDLED only
set(RENSGA_PSP_CPU_MHZ 333 CACHE STRING "PSP: CPU clock in MHz (222 or 333; bus = half)")
target_compile_definitions(rensga_platform_psp PRIVATE RENSGA_PSP_CPU_MHZ=${RENSGA_PSP_CPU_MHZ})
option(RENSGA_PSP_NATIVE_PAD "PSP: read the pad with sceCtrl (psp_pad::map) instead of the SDL game controller" OFF)
if(RENSGA_PSP_NATIVE_PAD)
  target_compile_definitions(rensga_platform_psp PRIVATE RENSGA_PSP_NATIVE_PAD=1)
endif()
