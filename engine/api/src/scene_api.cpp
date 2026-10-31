// F6 contract stubs (C0) for API 9 scenes, assets and saves and API 12 helpers: SceneManager additive / async, Resources,
// PlayerPrefs, SaveSystem, Profiler samples, Budget. Safe defaults until A3 implements them (and D2 the profiler feed). The
// signatures are final (rensga/core/scene_manager.h and rensga/Resources.h, PlayerPrefs.h, SaveSystem.h, Profiler.h, Budget.h).
//
// What already works: Single loads (they forward to the existing deferred LoadScene), GetActiveScene / GetSceneByName / Scene::
// name() / isLoaded() for the one loaded scene. Everything else fails safely and logs once.
#include "rensga/Budget.h"
#include "rensga/PlayerPrefs.h"
#include "rensga/Profiler.h"
#include "rensga/Resources.h"
#include "rensga/SaveSystem.h"
#include "rensga/core/log.h"
#include "rensga/core/scene_manager.h"

namespace rensga {

namespace {
void stubOnce(bool* flag, const char* what) {
  if (*flag) return;
  *flag = true;
  RLOG_WARN("api", "%s is not implemented yet (F6 contract stub)", what);
}
}  // namespace

// ---- SceneManager (TODO(A3 scenes): additive scenes, async loads with progress, sceneLoaded callbacks) --------------------
const char* Scene::name() const { return index >= 0 ? SceneManager::sceneName(static_cast<u32>(index)) : ""; }
bool Scene::isLoaded() const { return index >= 0 && index == SceneManager::activeSceneIndex(); }

bool AsyncOperation::IsValid() const { return false; }
Scalar AsyncOperation::progress() const { return 0; }
bool AsyncOperation::isDone() const { return false; }
bool AsyncOperation::allowSceneActivation() const { return true; }
void AsyncOperation::setAllowSceneActivation(bool) const {}

bool SceneManager::LoadScene(i32 buildIndex, LoadSceneMode mode) {
  static bool once = false;
  if (mode == LoadSceneMode::Single) return LoadScene(buildIndex);
  stubOnce(&once, "SceneManager::LoadScene(Additive)");
  return false;
}
bool SceneManager::LoadScene(const char* name, LoadSceneMode mode) {
  static bool once = false;
  if (mode == LoadSceneMode::Single) return LoadScene(name);
  stubOnce(&once, "SceneManager::LoadScene(Additive)");
  return false;
}
AsyncOperation SceneManager::LoadSceneAsync(i32, LoadSceneMode) {
  static bool once = false;
  stubOnce(&once, "SceneManager::LoadSceneAsync");
  return AsyncOperation();
}
AsyncOperation SceneManager::LoadSceneAsync(const char*, LoadSceneMode) {
  static bool once = false;
  stubOnce(&once, "SceneManager::LoadSceneAsync");
  return AsyncOperation();
}
AsyncOperation SceneManager::UnloadSceneAsync(i32) {
  static bool once = false;
  stubOnce(&once, "SceneManager::UnloadSceneAsync");
  return AsyncOperation();
}
AsyncOperation SceneManager::UnloadSceneAsync(const char*) {
  static bool once = false;
  stubOnce(&once, "SceneManager::UnloadSceneAsync");
  return AsyncOperation();
}
Scene SceneManager::GetActiveScene() {
  Scene s;
  s.index = activeSceneIndex();
  return s;
}
bool SceneManager::SetActiveScene(Scene scene) { return scene.isLoaded(); }
Scene SceneManager::GetSceneByName(const char* name) {
  Scene s;
  s.index = name ? sceneIndexByName(name) : -1;
  return s;
}
Scene SceneManager::GetSceneByBuildIndex(i32 buildIndex) {
  Scene s;
  if (buildIndex >= 0 && static_cast<u32>(buildIndex) < sceneCount()) s.index = buildIndex;
  return s;
}
u32 SceneManager::loadedSceneCount() { return activeSceneIndex() >= 0 ? 1u : 0u; }
Scene SceneManager::GetLoadedSceneAt(u32 index) { return index == 0 ? GetActiveScene() : Scene(); }
bool SceneManager::addSceneLoaded(SceneLoadedCallback) { return false; }
void SceneManager::removeSceneLoaded(SceneLoadedCallback) {}
bool SceneManager::addSceneUnloaded(SceneUnloadedCallback) { return false; }
void SceneManager::removeSceneUnloaded(SceneUnloadedCallback) {}

// ---- Resources (TODO(A3 assets): RSRC lookup, reference counts, DATA instances) --------------------------------------------
const void* ResourceHandle::dataPointer() const { return nullptr; }
ResourceHandle Resources::LoadRaw(const char*, PackResourceKind) { return ResourceHandle(); }
void Resources::Unload(ResourceHandle& handle) { handle = ResourceHandle(); }
u32 Resources::loadedCount() { return 0; }
u32 Resources::entryCount() { return 0; }

// ---- PlayerPrefs (TODO(A3 saves): fixed key table, persisted through the save store as slot file "prefs") ---------------------
void PlayerPrefs::SetInt(const char*, i32) {}
i32 PlayerPrefs::GetInt(const char*, i32 defaultValue) { return defaultValue; }
void PlayerPrefs::SetFloat(const char*, Scalar) {}
Scalar PlayerPrefs::GetFloat(const char*, Scalar defaultValue) { return defaultValue; }
void PlayerPrefs::SetString(const char*, const char*) {}
const char* PlayerPrefs::GetString(const char*, const char* defaultValue) { return defaultValue ? defaultValue : ""; }
bool PlayerPrefs::HasKey(const char*) { return false; }
void PlayerPrefs::DeleteKey(const char*) {}
void PlayerPrefs::DeleteAll() {}
bool PlayerPrefs::Save() { return false; }

// ---- SaveSystem (TODO(A3 saves): slot header {magic, version, size, crc, metadata}, PS2 card icon, PSP SAVEDATA) -----------
bool SaveSystem::Write(u32, u32, const void*, u32, const Metadata*) { return false; }
i32 SaveSystem::Read(u32, u32, void*, u32, MigrateFn) { return -1; }
bool SaveSystem::Exists(u32) { return false; }
bool SaveSystem::Delete(u32) { return false; }
bool SaveSystem::GetInfo(u32, SlotInfo* info) {
  if (info) *info = SlotInfo();
  return false;
}

// ---- Profiler (TODO(A3 helpers / D2 profiler): per-frame sample table feeding "rensga: prof" and the editor window) -------
void Profiler::BeginSample(const char*) {}
void Profiler::EndSample() {}
u32 Profiler::LastSampleMicros(const char*) { return 0; }

// ---- Budget (TODO(A3 helpers): CTGT limits, arena / VRAM / triangle counters, mixer voices) ---------------------------------
u32 Budget::memoryUsed() { return 0; }
u32 Budget::memoryLimit() { return 0; }
u32 Budget::vramUsed() { return 0; }
u32 Budget::vramLimit() { return 0; }
u32 Budget::trianglesLastFrame() { return 0; }
u32 Budget::trianglesLimit() { return 0; }
u32 Budget::voicesUsed() { return 0; }
u32 Budget::voicesLimit() { return 0; }
u32 Budget::percentUsed() { return 0; }

}  // namespace rensga
