add_library(rensga_core STATIC
  src/log.cpp src/memory.cpp src/math.cpp src/guid.cpp src/reflect.cpp src/world.cpp
  src/components.cpp src/pack.cpp src/input.cpp src/debug.cpp src/application.cpp src/loop.cpp src/filesystem.cpp src/time.cpp src/audio.cpp src/gameplay.cpp src/rigidbody2d.cpp src/scene_manager.cpp src/save.cpp
  src/coroutine.cpp src/unity_event.cpp src/reflect_method.cpp src/gizmos.cpp
  # --- F6 a2 begin ---
  src/physics3d_shapes.cpp src/physics3d_step.cpp
  # --- F6 a2 end ---
  src/lighting.cpp src/animation.cpp src/cull.cpp src/app_events.cpp src/dev_control.cpp src/system_services.cpp
  ${RENSGA_REFLECT_ENGINE_SRCS})
target_include_directories(rensga_core PUBLIC include)
# --- F6 a3 begin ---
target_sources(rensga_core PRIVATE src/scene_load.cpp src/runtime_stats.cpp src/script_samples.cpp)
# --- F6 a3 end ---
# --- F6 b2 begin ---
target_sources(rensga_core PRIVATE src/render_view.cpp src/particles.cpp src/line_renderer.cpp src/debug_draw.cpp)
# --- F6 b2 end ---
rensga_runtime_target(rensga_core)

# JSON load/save of reflected fields: host tools only.
if(RENSGA_BUILD_TESTS OR RENSGA_BUILD_TOOLS OR RENSGA_BUILD_EDITOR)
  add_library(rensga_reflect_json STATIC json/reflect_json.cpp)
  target_link_libraries(rensga_reflect_json PUBLIC rensga_core nlohmann_json::nlohmann_json)
  rensga_host_target(rensga_reflect_json)
endif()
# --- F6 B1 begin ---
target_sources(rensga_core PRIVATE src/prop_clip.cpp src/animator.cpp)
# --- F6 B1 end ---
