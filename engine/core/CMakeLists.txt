add_library(rensga_core STATIC
  src/log.cpp src/memory.cpp src/math.cpp src/guid.cpp src/reflect.cpp src/world.cpp
  src/components.cpp src/pack.cpp src/input.cpp src/debug.cpp src/application.cpp src/loop.cpp src/filesystem.cpp src/time.cpp src/audio.cpp src/gameplay.cpp src/scene_manager.cpp src/save.cpp
  src/coroutine.cpp src/unity_event.cpp src/reflect_method.cpp src/gizmos.cpp
  ${RENSGA_REFLECT_ENGINE_SRCS})
target_include_directories(rensga_core PUBLIC include)
rensga_runtime_target(rensga_core)

# JSON load/save of reflected fields: host tools only.
if(RENSGA_BUILD_TESTS OR RENSGA_BUILD_TOOLS OR RENSGA_BUILD_EDITOR)
  add_library(rensga_reflect_json STATIC json/reflect_json.cpp)
  target_link_libraries(rensga_reflect_json PUBLIC rensga_core nlohmann_json::nlohmann_json)
  rensga_host_target(rensga_reflect_json)
endif()
