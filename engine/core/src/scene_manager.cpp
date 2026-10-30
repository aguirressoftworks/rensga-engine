#include "rensga/core/scene_manager.h"

#include <string.h>

#include "rensga/core/gameplay.h"
#include "rensga/core/log.h"
#include "rensga/core/world.h"

namespace rensga {

namespace {
const PackView* g_pack = nullptr;
i32 g_active = 0;
i32 g_pending = -1;
}  // namespace

void SceneManager::bind(const PackView* pack, u32 loadedSceneIndex) {
  g_pack = pack;
  g_active = static_cast<i32>(loadedSceneIndex);
  g_pending = -1;
  Physics2D::loadFromPack(pack);
}
void SceneManager::unbind() { bind(nullptr, 0); }

u32 SceneManager::sceneCount() {
  if (!g_pack) return 0;
  u32 n = 0;
  g_pack->records<PackSceneRec>(pack_section::SCNE, &n);
  return n ? n : 1;
}

const char* SceneManager::sceneName(u32 index) {
  if (!g_pack) return "";
  u32 n = 0;
  const PackSceneRec* s = g_pack->records<PackSceneRec>(pack_section::SCNE, &n);
  return (s && index < n) ? g_pack->string(s[index].nameStr) : "";
}

i32 SceneManager::sceneIndexByName(const char* name) {
  if (!name) return -1;
  for (u32 i = 0; i < sceneCount(); ++i)
    if (strcmp(sceneName(i), name) == 0) return static_cast<i32>(i);
  return -1;
}

i32 SceneManager::activeSceneIndex() { return g_active; }
const char* SceneManager::activeSceneName() { return sceneName(static_cast<u32>(g_active)); }

bool SceneManager::LoadScene(i32 buildIndex) {
  if (buildIndex < 0 || static_cast<u32>(buildIndex) >= sceneCount()) {
    RLOG_ERROR("scene", "LoadScene(%d): no such scene (%u in the pack)", buildIndex, sceneCount());
    return false;
  }
  g_pending = buildIndex;
  return true;
}

bool SceneManager::LoadScene(const char* name) {
  const i32 i = sceneIndexByName(name);
  if (i < 0) {
    RLOG_ERROR("scene", "LoadScene(\"%s\"): no such scene", name ? name : "(null)");
    return false;
  }
  return LoadScene(i);
}

bool SceneManager::loadPending() { return g_pending >= 0; }

bool SceneManager::processPending(World& world) {
  if (g_pending < 0 || !g_pack) return false;
  const u32 index = static_cast<u32>(g_pending);
  g_pending = -1;
  world.destroyAllImmediate(true);
  Gameplay2D::resetTriggerMemory();
  Physics2D::loadFromPack(g_pack);
  UI::resetFocus();
  LoadedPack lp;
  if (!world.loadPack(*g_pack, &lp, index)) {
    RLOG_ERROR("scene", "loading scene %u failed", index);
    return false;
  }
  g_active = static_cast<i32>(index);
  RLOG_INFO("scene", "rensga: scene loaded index=%u name=%s objects=%u", index, sceneName(index), world.objectCount());
  return true;
}

}  // namespace rensga
