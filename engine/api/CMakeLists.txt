add_library(rensga_api STATIC src/physics.cpp src/hud.cpp src/sim.cpp src/testlevel.cpp src/audio.cpp)
target_include_directories(rensga_api PUBLIC include)
target_link_libraries(rensga_api PUBLIC rensga_core)
# F5.12: project builds see console_target.gen.h (ENGINE_PROFILE_*, ENGINE_FEATURE_*) through the API, so RENSGA_REQUIRES_FEATURE
# can gate calls. The api library itself does not include it (INTERFACE only).
target_include_directories(rensga_api INTERFACE ${RENSGA_GENERATED_DIR})
rensga_runtime_target(rensga_api)
