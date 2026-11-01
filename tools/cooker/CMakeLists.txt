# Per-profile defaults (limits, features) are data: data/profile_defaults.json is embedded into the cooker core.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/profile_defaults.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/data/profile_defaults.json" RENSGA_PROFILE_DEFAULTS_JSON)
configure_file(data/profile_defaults.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/profile_defaults.cpp" @ONLY)

# Shared cooker core (host only: exceptions allowed).
add_library(rensga_cooker_core STATIC
  "${CMAKE_CURRENT_BINARY_DIR}/profile_defaults.cpp" src/plugin_registry.cpp src/console_target.cpp src/primitives.cpp src/budget.cpp src/cook.cpp
  src/assets.cpp src/assets_cook.cpp src/import_settings.cpp src/resample.cpp src/default_font.cpp src/psp_format.cpp)
# --- F5 A1 begin ---
target_sources(rensga_cooker_core PRIVATE src/skin_split.cpp src/material.cpp src/lit_mesh.cpp)
# --- F5 A1 end ---
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

# --- F6 A1 begin ---
# Prefab file format + expansion (F6.01): shared by the cooker and the editor, so they live in the cooker core.
target_sources(rensga_cooker_core PRIVATE src/prefab_expand.cpp src/prefab_cook.cpp ${CMAKE_SOURCE_DIR}/editor/core/src/prefab_format.cpp)
target_include_directories(rensga_cooker_core PUBLIC ${CMAKE_SOURCE_DIR}/editor/core/include)
# --- F6 A1 end ---

# --- F6 a2 begin ---
# PHY3 (3D physics settings, F6.08).
target_sources(rensga_cooker_core PRIVATE src/physics_cook.cpp)
# --- F6 a2 end ---

# --- F6 a3 begin ---
# RSRC (Resources.Load names) and the additive scene flag (F6.11).
target_sources(rensga_cooker_core PRIVATE src/resources_cook.cpp)
# --- F6 a3 end ---

# --- F6 B3 begin ---
# Baked vertex lighting (F6.02): lights + AO into vertex colors at cook time.
target_sources(rensga_cooker_core PRIVATE src/vertex_bake.cpp)
# The ray casts are the one heavy loop of a cook: optimise it even in a Debug host tree (no fast-math, -ffp-contract=off stays global,
# so the bake gives the same bytes at every optimisation level).
set_source_files_properties(src/vertex_bake.cpp PROPERTIES COMPILE_OPTIONS "-O2")
# --- F6 B3 end ---

# --- F6 B1 begin ---
# Property clips and animator controllers (F6.03): shared by the cooker and the editor preview.
target_sources(rensga_cooker_core PRIVATE src/anim_cook.cpp)
# --- F6 B1 end ---
