# Shared cooker core (host only: exceptions allowed).
add_library(rensga_cooker_core STATIC
  src/plugin_registry.cpp src/console_target.cpp src/primitives.cpp src/budget.cpp src/cook.cpp
  src/assets.cpp src/assets_cook.cpp src/default_font.cpp)
target_include_directories(rensga_cooker_core PUBLIC include)
target_link_libraries(rensga_cooker_core PUBLIC rensga_core rensga_reflect_json nlohmann_json::nlohmann_json
                      PRIVATE rensga_stb_image rensga_cgltf)
rensga_host_target(rensga_cooker_core)

# Format plugins self-register; they are linked WHOLE_ARCHIVE so the registrar survives.
add_library(rensga_cooker_psp STATIC src/psp_plugin.cpp)
target_link_libraries(rensga_cooker_psp PUBLIC rensga_cooker_core)
rensga_host_target(rensga_cooker_psp)

add_library(rensga_cooker_pc STATIC src/pc_plugin.cpp)
target_link_libraries(rensga_cooker_pc PUBLIC rensga_cooker_core)
rensga_host_target(rensga_cooker_pc)
add_library(rensga_cooker_ps2 STATIC src/ps2_plugin.cpp)
target_link_libraries(rensga_cooker_ps2 PUBLIC rensga_cooker_core)
rensga_host_target(rensga_cooker_ps2)

# What consumers link: core + every cooker plugin.
add_library(rensga_cooker_lib INTERFACE)
target_link_libraries(rensga_cooker_lib INTERFACE rensga_cooker_core "$<LINK_LIBRARY:WHOLE_ARCHIVE,rensga_cooker_psp>"
                      "$<LINK_LIBRARY:WHOLE_ARCHIVE,rensga_cooker_pc>" "$<LINK_LIBRARY:WHOLE_ARCHIVE,rensga_cooker_ps2>")

# Project scripts (Assets/Scripts/*.cpp) register their reflected types here so scenes can be read.
# They derive from engine/core classes, so they are built with the runtime flags (-fno-rtti).
set(RENSGA_COOKER_SCRIPTS ${RENSGA_REFLECT_PROJECT_SRCS})
if(RENSGA_COOKER_SCRIPTS)
  add_library(rensga_cooker_scripts STATIC ${RENSGA_COOKER_SCRIPTS})
  target_link_libraries(rensga_cooker_scripts PUBLIC rensga_api rensga_core)
  rensga_runtime_target(rensga_cooker_scripts)
endif()

add_executable(rensga-cooker src/main.cpp)
target_link_libraries(rensga-cooker PRIVATE rensga_cooker_lib)
if(TARGET rensga_cooker_scripts)
  target_link_libraries(rensga-cooker PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,rensga_cooker_scripts>")
endif()
rensga_host_target(rensga-cooker)
