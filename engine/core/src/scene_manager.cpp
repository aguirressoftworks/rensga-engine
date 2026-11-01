#include "rensga/core/scene_manager.h"

#include <string.h>

#include "rensga/core/gameplay.h"
#include "rensga/core/log.h"
#include "rensga/core/reflect.h"
#include "rensga/core/world.h"

namespace rensga {

// F2.05 + F6 (A3, API 9). See scene_manager.h for the rules. State is a handful of statics (no heap):
//  - g_loadedMask: bit i = scene i of the SCNE list is loaded (up to 32 scenes);
//  - g_pending: the deferred single LoadScene (last call wins, as before);
//  - g_reqs: deferred additive loads and unloads, in call order;
//  - g_ops: the AsyncOperation table (4 slots); one async operation works at a time, the others queue by serial.
namespace {
constexpr u32 kMaxScenes = 32;
constexpr u32 kMaxRequests = 8;
constexpr u32 kMaxCallbacks = 4;
constexpr u32 kNoOp = 0xFFFFFFFFu;
constexpr Scalar kLoadShare = 0.9f;  // progress while loading; the last tenth is the activation

enum class ReqKind : u8 { AdditiveLoad, Unload };
struct Request {
  ReqKind kind = ReqKind::AdditiveLoad;
  i32 scene = -1;
};
enum class OpState : u8 { Free, Queued, Running, Ready, Done };
struct Op {
  OpState state = OpState::Free;
  u32 serial = 0;
  bool unload = false;
  bool allow = true;
  bool ok = true;
  LoadSceneMode mode = LoadSceneMode::Single;
  i32 scene = -1;
  Scalar progress = 0;
  u32 frames = 0;
  // single-mode "prepare" pass: a validation sweep over the scene's components, a chunk per frame
  u32 prepCursor = 0, prepDone = 0, prepTotal = 0;
};

const PackView* g_pack = nullptr;
i32 g_active = 0;
u32 g_loadedMask = 1;
i32 g_pending = -1;
Request g_reqs[kMaxRequests];
u32 g_reqCount = 0;
Op g_ops[AsyncOperation::kMaxAsyncOperations];
u32 g_serial = 0;
World::SceneLoadJob g_job;
u32 g_jobOp = kNoOp;
u32 g_chunk = 32;
u32 g_loads = 0;
SceneManager::SceneLoadedCallback g_onLoaded[kMaxCallbacks] = {};
SceneManager::SceneUnloadedCallback g_onUnloaded[kMaxCallbacks] = {};

bool isLoaded(i32 i) { return i >= 0 && i < static_cast<i32>(kMaxScenes) && (g_loadedMask & (1u << i)); }
bool validIndex(i32 i) { return i >= 0 && static_cast<u32>(i) < SceneManager::sceneCount() && i < static_cast<i32>(kMaxScenes); }

u32 loadedCount() {
  u32 n = 0;
  for (u32 i = 0; i < kMaxScenes; ++i)
    if (g_loadedMask & (1u << i)) ++n;
  return n;
}

void fireLoaded(i32 index, LoadSceneMode mode) {
  Scene s;
  s.index = index;
  for (u32 i = 0; i < kMaxCallbacks; ++i)
    if (g_onLoaded[i]) g_onLoaded[i](s, mode);
}
void fireUnloaded(i32 index) {
  Scene s;
  s.index = index;
  for (u32 i = 0; i < kMaxCallbacks; ++i)
    if (g_onUnloaded[i]) g_onUnloaded[i](s);
}

void pickActive() {
  if (isLoaded(g_active)) return;
  g_active = -1;
  for (i32 i = 0; i < static_cast<i32>(kMaxScenes); ++i)
    if (isLoaded(i)) { g_active = i; return; }
}

void resetAll() {
  g_pending = -1;
  g_reqCount = 0;
  for (u32 i = 0; i < AsyncOperation::kMaxAsyncOperations; ++i) g_ops[i] = Op();
  g_job = World::SceneLoadJob();
  g_jobOp = kNoOp;
  g_loads = 0;
}

// The one-shot path every plain LoadScene takes (behaviour unchanged since F2.05).
bool loadSingleNow(World& world, u32 index) {
  if (g_jobOp != kNoOp) {  // a half built additive scene would be destroyed with the rest
    world.abortSceneLoad(g_job);
    g_ops[g_jobOp].state = OpState::Done;
    g_ops[g_jobOp].ok = false;
    g_jobOp = kNoOp;
  }
  for (i32 i = 0; i < static_cast<i32>(kMaxScenes); ++i)
    if (isLoaded(i)) fireUnloaded(i);
  world.destroyAllImmediate(true);
  Gameplay2D::resetTriggerMemory();
  Physics2D::loadFromPack(g_pack);
  UI::resetFocus();
  g_loadedMask = 0;
  LoadedPack lp;
  if (!world.loadPack(*g_pack, &lp, index)) {
    RLOG_ERROR("scene", "loading scene %u failed", index);
    g_active = -1;
    return false;
  }
  g_loadedMask = 1u << index;
  g_active = static_cast<i32>(index);
  ++g_loads;
  RLOG_INFO("scene", "rensga: scene loaded index=%u name=%s objects=%u", index, SceneManager::sceneName(index), world.objectCount());
  fireLoaded(static_cast<i32>(index), LoadSceneMode::Single);
  return true;
}

bool loadAdditiveNow(World& world, u32 index) {
  if (isLoaded(static_cast<i32>(index))) {
    RLOG_ERROR("scene", "LoadScene(\"%s\", Additive): that scene is already loaded", SceneManager::sceneName(index));
    return false;
  }
  char why[96] = {};
  if (!world.sceneFits(*g_pack, index, why, sizeof(why))) {
    RLOG_ERROR("scene", "additive load of \"%s\" refused: %s", SceneManager::sceneName(index), why);
    return false;
  }
  LoadedPack lp;
  if (!world.loadPack(*g_pack, &lp, index)) {
    world.destroyScene(static_cast<i32>(index));
    RLOG_ERROR("scene", "additive load of \"%s\" failed", SceneManager::sceneName(index));
    return false;
  }
  g_loadedMask |= 1u << index;
  if (g_active < 0) g_active = static_cast<i32>(index);
  ++g_loads;
  RLOG_INFO("scene", "rensga: scene loaded additive index=%u name=%s objects=%u", index, SceneManager::sceneName(index), world.objectCount());
  fireLoaded(static_cast<i32>(index), LoadSceneMode::Additive);
  return true;
}

bool unloadNow(World& world, i32 index) {
  if (!isLoaded(index)) {
    RLOG_ERROR("scene", "UnloadScene: scene %d is not loaded", index);
    return false;
  }
  if (loadedCount() <= 1) {
    RLOG_ERROR("scene", "UnloadScene: \"%s\" is the only loaded scene (load another one first)", SceneManager::sceneName(static_cast<u32>(index)));
    return false;
  }
  if (g_jobOp != kNoOp && g_job.scene == static_cast<u32>(index)) {
    world.abortSceneLoad(g_job);
    g_ops[g_jobOp].state = OpState::Done;
    g_ops[g_jobOp].ok = false;
    g_jobOp = kNoOp;
  }
  const u32 n = world.destroyScene(index);
  g_loadedMask &= ~(1u << index);
  pickActive();
  RLOG_INFO("scene", "rensga: scene unloaded index=%d name=%s roots=%u", index, SceneManager::sceneName(static_cast<u32>(index)), n);
  fireUnloaded(index);
  return true;
}

void finishOp(Op& op, bool ok) {
  op.state = OpState::Done;
  op.ok = ok;
  op.progress = ok ? 1.0f : op.progress;
}

// Validation sweep for a Single async load: the checks loadPack makes on the target scene's components, a chunk per frame.
bool prepareChunk(Op& op, u32 items, bool* finished) {
  const PackView& pk = *g_pack;
  u32 compCount = 0, sceneCount = 0;
  const PackComponent* comps = pk.records<PackComponent>(pack_section::COMP, &compCount);
  const PackSceneRec* scenes = pk.records<PackSceneRec>(pack_section::SCNE, &sceneCount);
  const PackSection* fdat = pk.find(pack_section::FDAT);
  u32 first = 0, count = 0xFFFFFFFFu;
  if (scenes && static_cast<u32>(op.scene) < sceneCount) {
    first = scenes[op.scene].firstObject;
    count = scenes[op.scene].objectCount;
  }
  auto inScene = [&](const PackComponent& c) { return c.objectIndex >= first && c.objectIndex - first < count; };
  if (op.prepTotal == 0) {  // first call: how many components this scene has
    u32 n = 0;
    for (u32 i = 0; i < compCount; ++i)
      if (inScene(comps[i])) ++n;
    op.prepTotal = n ? n : 1;
    op.prepDone = 0;
  }
  u32 n = 0;
  while (op.prepCursor < compCount && n < items) {
    const PackComponent& c = comps[op.prepCursor++];
    if (!inScene(c)) continue;
    ++n;
    ++op.prepDone;
    if (!TypeRegistry::instance().findByHash(c.typeHash)) {
      RLOG_ERROR("scene", "async load: unknown component type hash 0x%08x", static_cast<unsigned>(c.typeHash));
      return false;
    }
    if (!fdat || c.fieldOffset > fdat->size || c.fieldSize > fdat->size - c.fieldOffset) {
      RLOG_ERROR("scene", "async load: component %u has bad offsets", op.prepCursor - 1);
      return false;
    }
  }
  *finished = op.prepCursor >= compCount;
  return true;
}

void advanceAsync(World& world) {
  // Start the oldest queued operation when none runs.
  if (g_jobOp == kNoOp) {
    u32 best = kNoOp;
    for (u32 i = 0; i < AsyncOperation::kMaxAsyncOperations; ++i) {
      if (g_ops[i].state != OpState::Queued && g_ops[i].state != OpState::Running && g_ops[i].state != OpState::Ready) continue;
      if (best == kNoOp || g_ops[i].serial < g_ops[best].serial) best = i;
    }
    if (best == kNoOp) return;
    g_jobOp = best;
  }
  Op& op = g_ops[g_jobOp];
  const u32 slot = g_jobOp;
  ++op.frames;
  if (op.unload) {
    const bool ok = unloadNow(world, op.scene);
    finishOp(op, ok);
    g_jobOp = kNoOp;
    return;
  }
  if (op.mode == LoadSceneMode::Single) {
    if (op.state == OpState::Queued) op.state = OpState::Running;
    if (op.state == OpState::Running) {
      bool swept = false;
      if (!prepareChunk(op, g_chunk ? g_chunk : 1, &swept)) {
        finishOp(op, false);
        g_jobOp = kNoOp;
        return;
      }
      op.progress = kLoadShare * static_cast<Scalar>(op.prepDone) / static_cast<Scalar>(op.prepTotal);
      if (swept || op.prepDone >= op.prepTotal) {
        op.progress = kLoadShare;
        op.state = OpState::Ready;
      }
      return;
    }
    if (op.state == OpState::Ready && op.allow) {
      const bool ok = loadSingleNow(world, static_cast<u32>(op.scene));
      RLOG_INFO("scene", "rensga: async load done index=%d mode=Single ok=%d frames=%u", op.scene, ok ? 1 : 0, op.frames);
      finishOp(op, ok);
      g_jobOp = kNoOp;
    }
    return;
  }
  // Additive: build the scene a chunk at a time into the running world.
  if (op.state == OpState::Queued) {
    if (isLoaded(op.scene)) {
      RLOG_ERROR("scene", "LoadSceneAsync(\"%s\", Additive): that scene is already loaded", SceneManager::sceneName(static_cast<u32>(op.scene)));
      finishOp(op, false);
      g_jobOp = kNoOp;
      return;
    }
    char why[96] = {};
    if (!world.beginSceneLoad(*g_pack, static_cast<u32>(op.scene), g_job, why, sizeof(why))) {
      RLOG_ERROR("scene", "async additive load of \"%s\" refused: %s", SceneManager::sceneName(static_cast<u32>(op.scene)), why);
      finishOp(op, false);
      g_jobOp = kNoOp;
      return;
    }
    op.state = OpState::Running;
  }
  if (op.state == OpState::Running) {
    const bool built = world.stepSceneLoad(g_job, g_chunk ? g_chunk : 1);
    if (!g_job.active) {  // the step failed and aborted
      finishOp(op, false);
      g_jobOp = kNoOp;
      return;
    }
    op.progress = kLoadShare * static_cast<Scalar>(g_job.done) / static_cast<Scalar>(g_job.total);
    if (built) {
      op.progress = kLoadShare;
      op.state = OpState::Ready;
    }
    return;
  }
  if (op.state == OpState::Ready && op.allow) {
    const bool ok = world.finishSceneLoad(g_job);
    if (ok) {
      g_loadedMask |= 1u << op.scene;
      if (g_active < 0) g_active = op.scene;
      ++g_loads;
      RLOG_INFO("scene", "rensga: scene loaded additive index=%d name=%s objects=%u (async, %u frames)", op.scene,
                SceneManager::sceneName(static_cast<u32>(op.scene)), world.objectCount(), op.frames);
      fireLoaded(op.scene, LoadSceneMode::Additive);
    }
    finishOp(op, ok);
    g_jobOp = kNoOp;
  }
  (void)slot;
}

AsyncOperation startOp(bool unload, i32 scene, LoadSceneMode mode) {
  AsyncOperation h;
  if (!g_pack || !validIndex(scene)) {
    RLOG_ERROR("scene", "%s: no such scene (%d)", unload ? "UnloadSceneAsync" : "LoadSceneAsync", scene);
    return h;
  }
  const u32 slot = g_serial % AsyncOperation::kMaxAsyncOperations;
  Op& op = g_ops[slot];
  if (op.state == OpState::Queued || op.state == OpState::Running || op.state == OpState::Ready) {
    RLOG_ERROR("scene", "too many asynchronous scene operations at once (%u)", AsyncOperation::kMaxAsyncOperations);
    return h;
  }
  op = Op();
  op.serial = ++g_serial;
  op.state = OpState::Queued;
  op.unload = unload;
  op.scene = scene;
  op.mode = mode;
  h.slot = slot;
  h.serial = op.serial;
  return h;
}

const Op* opOf(const AsyncOperation& h) {
  if (h.slot >= AsyncOperation::kMaxAsyncOperations) return nullptr;
  const Op& op = g_ops[h.slot];
  return (op.state != OpState::Free && op.serial == h.serial) ? &op : nullptr;
}
}  // namespace

// ---- Scene / AsyncOperation --------------------------------------------------------------------------------------------------
const char* Scene::name() const { return index >= 0 ? SceneManager::sceneName(static_cast<u32>(index)) : ""; }
bool Scene::isLoaded() const { return SceneManager::isSceneLoaded(index); }

bool AsyncOperation::IsValid() const { return opOf(*this) != nullptr; }
Scalar AsyncOperation::progress() const {
  const Op* op = opOf(*this);
  return op ? op->progress : 0;
}
bool AsyncOperation::isDone() const {
  const Op* op = opOf(*this);
  return op && op->state == OpState::Done;
}
bool AsyncOperation::allowSceneActivation() const {
  const Op* op = opOf(*this);
  return op ? op->allow : true;
}
void AsyncOperation::setAllowSceneActivation(bool allow) const {
  const Op* op = opOf(*this);
  if (op) const_cast<Op*>(op)->allow = allow;
}

// ---- SceneManager ----------------------------------------------------------------------------------------------------------
void SceneManager::bind(const PackView* pack, u32 loadedSceneIndex) {
  g_pack = pack;
  g_active = static_cast<i32>(loadedSceneIndex);
  g_loadedMask = loadedSceneIndex < kMaxScenes ? (1u << loadedSceneIndex) : 0;
  resetAll();
  Physics2D::loadFromPack(pack);
}
void SceneManager::unbind() { bind(nullptr, 0); }

const PackView* SceneManager::pack() { return g_pack; }

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
const char* SceneManager::activeSceneName() { return g_active >= 0 ? sceneName(static_cast<u32>(g_active)) : ""; }
bool SceneManager::isSceneLoaded(i32 buildIndex) { return isLoaded(buildIndex); }
void SceneManager::setAsyncChunk(u32 items) { g_chunk = items ? items : 1; }
u32 SceneManager::asyncChunk() { return g_chunk; }
u32 SceneManager::loadsCompleted() { return g_loads; }

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

bool SceneManager::LoadScene(i32 buildIndex, LoadSceneMode mode) {
  if (mode == LoadSceneMode::Single) return LoadScene(buildIndex);
  if (!validIndex(buildIndex)) {
    RLOG_ERROR("scene", "LoadScene(%d, Additive): no such scene (%u in the pack)", buildIndex, sceneCount());
    return false;
  }
  if (g_reqCount >= kMaxRequests) {
    RLOG_ERROR("scene", "LoadScene(Additive): too many scene requests in one frame (%u)", kMaxRequests);
    return false;
  }
  g_reqs[g_reqCount].kind = ReqKind::AdditiveLoad;
  g_reqs[g_reqCount].scene = buildIndex;
  ++g_reqCount;
  return true;
}

bool SceneManager::LoadScene(const char* name, LoadSceneMode mode) {
  const i32 i = sceneIndexByName(name);
  if (i < 0) {
    RLOG_ERROR("scene", "LoadScene(\"%s\"): no such scene", name ? name : "(null)");
    return false;
  }
  return LoadScene(i, mode);
}

AsyncOperation SceneManager::LoadSceneAsync(i32 buildIndex, LoadSceneMode mode) { return startOp(false, buildIndex, mode); }
AsyncOperation SceneManager::LoadSceneAsync(const char* name, LoadSceneMode mode) {
  const i32 i = sceneIndexByName(name);
  if (i < 0) RLOG_ERROR("scene", "LoadSceneAsync(\"%s\"): no such scene", name ? name : "(null)");
  return startOp(false, i, mode);
}
AsyncOperation SceneManager::UnloadSceneAsync(i32 buildIndex) { return startOp(true, buildIndex, LoadSceneMode::Additive); }
AsyncOperation SceneManager::UnloadSceneAsync(const char* name) {
  const i32 i = sceneIndexByName(name);
  if (i < 0) RLOG_ERROR("scene", "UnloadSceneAsync(\"%s\"): no such scene", name ? name : "(null)");
  return startOp(true, i, LoadSceneMode::Additive);
}

Scene SceneManager::GetActiveScene() {
  Scene s;
  s.index = g_active;
  return s;
}
bool SceneManager::SetActiveScene(Scene scene) {
  if (!isLoaded(scene.index)) return false;
  g_active = scene.index;
  return true;
}
Scene SceneManager::GetSceneByName(const char* name) {
  Scene s;
  s.index = name ? sceneIndexByName(name) : -1;
  return s;
}
Scene SceneManager::GetSceneByBuildIndex(i32 buildIndex) {
  Scene s;
  if (validIndex(buildIndex)) s.index = buildIndex;
  return s;
}
u32 SceneManager::loadedSceneCount() { return loadedCount(); }
Scene SceneManager::GetLoadedSceneAt(u32 index) {
  Scene s;
  u32 n = 0;
  for (i32 i = 0; i < static_cast<i32>(kMaxScenes); ++i) {
    if (!isLoaded(i)) continue;
    if (n++ == index) { s.index = i; break; }
  }
  return s;
}

bool SceneManager::addSceneLoaded(SceneLoadedCallback cb) {
  if (!cb) return false;
  for (u32 i = 0; i < kMaxCallbacks; ++i)
    if (g_onLoaded[i] == cb) return true;
  for (u32 i = 0; i < kMaxCallbacks; ++i)
    if (!g_onLoaded[i]) { g_onLoaded[i] = cb; return true; }
  return false;
}
void SceneManager::removeSceneLoaded(SceneLoadedCallback cb) {
  for (u32 i = 0; i < kMaxCallbacks; ++i)
    if (g_onLoaded[i] == cb) g_onLoaded[i] = nullptr;
}
bool SceneManager::addSceneUnloaded(SceneUnloadedCallback cb) {
  if (!cb) return false;
  for (u32 i = 0; i < kMaxCallbacks; ++i)
    if (g_onUnloaded[i] == cb) return true;
  for (u32 i = 0; i < kMaxCallbacks; ++i)
    if (!g_onUnloaded[i]) { g_onUnloaded[i] = cb; return true; }
  return false;
}
void SceneManager::removeSceneUnloaded(SceneUnloadedCallback cb) {
  for (u32 i = 0; i < kMaxCallbacks; ++i)
    if (g_onUnloaded[i] == cb) g_onUnloaded[i] = nullptr;
}

bool SceneManager::loadPending() { return g_pending >= 0 || g_reqCount > 0; }

bool SceneManager::processPending(World& world) {
  if (!g_pack) return false;
  bool loaded = false;
  if (g_pending >= 0) {
    const u32 index = static_cast<u32>(g_pending);
    g_pending = -1;
    g_reqCount = 0;  // additive requests made before a Single load die with the scenes they would join
    loaded = loadSingleNow(world, index);
  }
  if (g_reqCount > 0) {
    Request reqs[kMaxRequests];
    const u32 n = g_reqCount;
    memcpy(reqs, g_reqs, sizeof(Request) * n);
    g_reqCount = 0;
    for (u32 i = 0; i < n; ++i) {
      if (reqs[i].kind == ReqKind::AdditiveLoad) loaded = loadAdditiveNow(world, static_cast<u32>(reqs[i].scene)) || loaded;
      else unloadNow(world, reqs[i].scene);
    }
  }
  const u32 before = g_loads;
  advanceAsync(world);
  return loaded || g_loads != before;
}

}  // namespace rensga
