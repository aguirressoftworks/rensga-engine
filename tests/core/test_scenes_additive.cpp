// F6.11 (A3, API 9): SceneManager additive loads, async loads with progress, unload, active scene, sceneLoaded / sceneUnloaded,
// DontDestroyOnLoad across Single loads, memory refusal. Built on an in-memory pack (scene_pack.h), driven by GameLoop::frame.
#include "rensga/Rensga.h"
#include "rensga/core/scene_manager.h"
#include "rensga_test.h"
#include "scene_pack.h"

using namespace rensga;

namespace {
struct ScProbe : Behaviour {
  REFLECT(ScProbe)
  SERIALIZE_FIELD(i32, id, 0)
  static inline int awakes = 0, starts = 0, destroys = 0;
  void Awake() override { ++awakes; }
  void Start() override { ++starts; }
  void OnDestroy() override { ++destroys; }
};

struct Rig {
  Arena arena;
  World world;
  GameLoop loop;
  VirtualGamepad pad;
  scenepack::Builder b;
  u8* mem = nullptr;
  PackView view;
  explicit Rig(u32 maxObjects = 32, u32 maxPerType = 32) {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = maxObjects;
    c.maxComponentsPerType = maxPerType;
    world.init(c, arena);
    loop.init(world);
    ScProbe::awakes = ScProbe::starts = ScProbe::destroys = 0;
  }
  void finish(u32 first = 0) {
    size_t n = 0;
    mem = b.build(&n);
    CHECK(view.open(mem, n, "scenes-test"));
    SceneManager::bind(&view, first);
    LoadedPack lp;
    CHECK(world.loadPack(view, &lp, first));
  }
  void frames(int n) {
    for (int i = 0; i < n; ++i) loop.frame(1.0f / 60.0f, pad);
  }
  ~Rig() {
    SceneManager::unbind();
    world.shutdown();
    free(mem);
  }
};

using scenepack::Obj;
Obj probe(const char* name, int parent = -1, i32 id = 0) {
  Obj o;
  o.name = name;
  o.parent = parent;
  o.component = ScProbe::staticTypeInfo();
  o.firstI32 = id;
  return o;
}
void addThreeScenes(Rig& r) {
  r.b.addScene("Level1", {probe("Hero", -1, 1), probe("Hand", 0, 2), probe("Enemy", -1, 3)});
  r.b.addScene("Hud", {probe("Score", -1, 10), probe("Life", -1, 11)});
  r.b.addScene("Level2", {probe("Boss", -1, 20), probe("Door", -1, 21), probe("Key", 1, 22), probe("Lamp", -1, 23)});
}
int loadedCbs = 0, unloadedCbs = 0;
i32 lastLoaded = -1;
LoadSceneMode lastMode = LoadSceneMode::Single;
void onLoaded(Scene s, LoadSceneMode m) { ++loadedCbs; lastLoaded = s.index; lastMode = m; }
void onUnloaded(Scene) { ++unloadedCbs; }
}  // namespace

TEST(scene_additive_load_keeps_the_first_scene) {
  Rig r;
  addThreeScenes(r);
  r.finish(0);
  r.frames(1);
  CHECK_EQ(r.world.objectCount(), 3u);
  CHECK_EQ(SceneManager::loadedSceneCount(), 1u);
  CHECK(SceneManager::LoadScene("Hud", LoadSceneMode::Additive));
  CHECK_EQ(r.world.objectCount(), 3u);  // deferred to the end of the frame
  r.frames(1);
  CHECK_EQ(r.world.objectCount(), 5u);
  CHECK_EQ(SceneManager::loadedSceneCount(), 2u);
  CHECK(SceneManager::GetSceneByName("Hud").isLoaded());
  CHECK(SceneManager::GetSceneByName("Level1").isLoaded());
  CHECK(!SceneManager::GetSceneByName("Level2").isLoaded());
  CHECK_EQ(SceneManager::GetActiveScene().buildIndex(), 0);  // an additive load does not change the active scene
  CHECK(GameObject::Find("Score") != nullptr && GameObject::Find("Hero") != nullptr);
  CHECK_EQ(GameObject::Find("Score")->sceneIndex, 1);
  CHECK_EQ(GameObject::Find("Hero")->sceneIndex, 0);
  SceneManager::LoadScene("Hud", LoadSceneMode::Additive);  // already loaded: refused with a log line
  r.frames(1);
  CHECK_EQ(r.world.objectCount(), 5u);
}

TEST(scene_additive_objects_wake_like_a_single_load) {
  Rig r;
  addThreeScenes(r);
  r.finish(0);
  r.frames(1);
  const int awakes = ScProbe::awakes, starts = ScProbe::starts;
  CHECK(SceneManager::LoadScene(2, LoadSceneMode::Additive));
  r.frames(1);
  CHECK_EQ(ScProbe::awakes - awakes, 4);  // Awake ran when the scene arrived
  CHECK_EQ(ScProbe::starts - starts, 0);  // Start waits for the next frame's first update
  r.frames(1);
  CHECK_EQ(ScProbe::starts - starts, 4);
}

TEST(scene_unload_destroys_only_that_scene) {
  Rig r;
  addThreeScenes(r);
  r.finish(0);
  CHECK(SceneManager::LoadScene(1, LoadSceneMode::Additive));
  CHECK(SceneManager::LoadScene(2, LoadSceneMode::Additive));
  r.frames(2);
  CHECK_EQ(r.world.objectCount(), 9u);
  const int destroys = ScProbe::destroys;
  AsyncOperation op = SceneManager::UnloadSceneAsync("Hud");
  CHECK(op.IsValid() && !op.isDone());
  r.frames(2);
  CHECK(op.isDone() && op.progress() == 1.0f);
  CHECK_EQ(r.world.objectCount(), 7u);
  CHECK_EQ(ScProbe::destroys - destroys, 2);
  CHECK(!SceneManager::GetSceneByName("Hud").isLoaded());
  CHECK(GameObject::Find("Score") == nullptr && GameObject::Find("Boss") != nullptr);
  // the last loaded scene cannot be unloaded
  AsyncOperation a = SceneManager::UnloadSceneAsync("Level2");
  r.frames(2);
  CHECK(a.isDone());
  AsyncOperation bad = SceneManager::UnloadSceneAsync("Level1");
  r.frames(2);
  CHECK(bad.isDone());
  CHECK_EQ(SceneManager::loadedSceneCount(), 1u);
}

TEST(scene_active_scene_moves_when_it_is_unloaded) {
  Rig r;
  addThreeScenes(r);
  r.finish(0);
  SceneManager::LoadScene(1, LoadSceneMode::Additive);
  r.frames(2);
  CHECK(SceneManager::SetActiveScene(SceneManager::GetSceneByName("Hud")));
  CHECK_EQ(SceneManager::GetActiveScene().buildIndex(), 1);
  CHECK(!SceneManager::SetActiveScene(SceneManager::GetSceneByName("Level2")));  // not loaded
  SceneManager::UnloadSceneAsync(1);
  r.frames(2);
  CHECK_EQ(SceneManager::GetActiveScene().buildIndex(), 0);
}

TEST(scene_callbacks_fire_for_loads_and_unloads) {
  Rig r;
  addThreeScenes(r);
  r.finish(0);
  loadedCbs = unloadedCbs = 0;
  CHECK(SceneManager::addSceneLoaded(&onLoaded));
  CHECK(SceneManager::addSceneUnloaded(&onUnloaded));
  SceneManager::LoadScene(1, LoadSceneMode::Additive);
  r.frames(1);
  CHECK_EQ(loadedCbs, 1);
  CHECK(lastLoaded == 1 && lastMode == LoadSceneMode::Additive);
  SceneManager::UnloadSceneAsync(1);
  r.frames(2);
  CHECK_EQ(unloadedCbs, 1);
  SceneManager::LoadScene(2);  // Single: the loaded scene unloads, the new one loads
  r.frames(1);
  CHECK_EQ(loadedCbs, 2);
  CHECK(lastLoaded == 2 && lastMode == LoadSceneMode::Single);
  CHECK_EQ(unloadedCbs, 2);
  SceneManager::removeSceneLoaded(&onLoaded);
  SceneManager::removeSceneUnloaded(&onUnloaded);
}

TEST(scene_single_load_replaces_every_scene_but_keeps_dont_destroy_on_load) {
  Rig r;
  addThreeScenes(r);
  r.finish(0);
  SceneManager::LoadScene(1, LoadSceneMode::Additive);
  r.frames(2);
  DontDestroyOnLoad(GameObject::Find("Score"));
  CHECK_EQ(GameObject::Find("Score")->sceneIndex, -1);
  SceneManager::LoadScene("Level2");
  r.frames(1);
  CHECK_EQ(SceneManager::loadedSceneCount(), 1u);
  CHECK_EQ(SceneManager::GetActiveScene().buildIndex(), 2);
  CHECK(GameObject::Find("Score") != nullptr);  // kept
  CHECK(GameObject::Find("Life") == nullptr && GameObject::Find("Hero") == nullptr && GameObject::Find("Boss") != nullptr);
  CHECK_EQ(r.world.objectCount(), 5u);
}

TEST(scene_async_additive_progresses_over_frames_and_waits_for_activation) {
  Rig r;
  addThreeScenes(r);
  r.finish(0);
  r.frames(1);
  SceneManager::setAsyncChunk(2);
  AsyncOperation op = SceneManager::LoadSceneAsync("Level2", LoadSceneMode::Additive);
  CHECK(op.IsValid());
  op.setAllowSceneActivation(false);
  CHECK(!op.allowSceneActivation());
  const u32 before = r.world.objectCount();
  Scalar last = -1;
  int frames = 0;
  while (op.progress() < 0.89f && frames < 50) {
    r.frames(1);
    ++frames;
    CHECK(op.progress() >= last);
    last = op.progress();
    CHECK(!op.isDone());
  }
  CHECK(frames >= 3);  // 4 objects + 4 links + 4 components at 2 per frame
  CHECK(op.progress() >= 0.89f && op.progress() < 0.91f);
  CHECK(!SceneManager::GetSceneByName("Level2").isLoaded());
  // built but inactive and not awake: nothing of it runs yet
  const int awakes = ScProbe::awakes;
  r.frames(3);
  CHECK(!op.isDone());
  CHECK_EQ(ScProbe::awakes, awakes);
  CHECK(GameObject::Find("Boss") != nullptr && !GameObject::Find("Boss")->activeInHierarchy());  // built, but inactive
  op.setAllowSceneActivation(true);
  r.frames(2);
  CHECK(op.isDone() && op.progress() == 1.0f);
  CHECK(SceneManager::GetSceneByName("Level2").isLoaded());
  CHECK_EQ(r.world.objectCount(), before + 4);
  CHECK_EQ(ScProbe::awakes - awakes, 4);
  CHECK(GameObject::Find("Boss") != nullptr && GameObject::Find("Boss")->activeInHierarchy());
  SceneManager::setAsyncChunk(32);
}

TEST(scene_async_single_replaces_at_activation) {
  Rig r;
  addThreeScenes(r);
  r.finish(0);
  r.frames(1);
  SceneManager::setAsyncChunk(1);
  AsyncOperation op = SceneManager::LoadSceneAsync(2);
  op.setAllowSceneActivation(false);
  r.frames(8);
  CHECK(!op.isDone() && op.progress() >= 0.89f);
  CHECK(GameObject::Find("Hero") != nullptr);  // the old scene still runs
  op.setAllowSceneActivation(true);
  r.frames(2);
  CHECK(op.isDone());
  CHECK(GameObject::Find("Hero") == nullptr && GameObject::Find("Boss") != nullptr);
  CHECK_EQ(SceneManager::GetActiveScene().buildIndex(), 2);
  SceneManager::setAsyncChunk(32);
}

TEST(scene_async_operations_queue_and_handles_expire) {
  Rig r;
  addThreeScenes(r);
  r.finish(0);
  AsyncOperation a = SceneManager::LoadSceneAsync(1, LoadSceneMode::Additive);
  AsyncOperation b = SceneManager::LoadSceneAsync(2, LoadSceneMode::Additive);
  CHECK(a.IsValid() && b.IsValid());
  r.frames(10);
  CHECK(a.isDone() && b.isDone());
  CHECK_EQ(SceneManager::loadedSceneCount(), 3u);
  CHECK_EQ(r.world.objectCount(), 9u);
  AsyncOperation c = SceneManager::UnloadSceneAsync(1);
  AsyncOperation d = SceneManager::UnloadSceneAsync(2);
  AsyncOperation e = SceneManager::UnloadSceneAsync(0);  // 0 is the last scene once 1 and 2 are gone: refused
  AsyncOperation f = SceneManager::UnloadSceneAsync(0);
  r.frames(10);
  CHECK(c.isDone() && d.isDone());
  CHECK(!a.IsValid());  // slot reused by the 5th operation: old handles read as invalid
  (void)e;
  (void)f;
  CHECK(!SceneManager::LoadSceneAsync(99).IsValid());
  CHECK(!SceneManager::LoadSceneAsync("Nope").IsValid());
}

TEST(scene_additive_refused_when_the_world_is_too_small) {
  Rig r(6, 32);  // 3 objects live, Level2 has 4
  addThreeScenes(r);
  r.finish(0);
  r.frames(1);
  CHECK(SceneManager::LoadScene("Level2", LoadSceneMode::Additive));
  r.frames(1);
  CHECK_EQ(r.world.objectCount(), 3u);  // refused, nothing half loaded
  CHECK(!SceneManager::GetSceneByName("Level2").isLoaded());
  AsyncOperation op = SceneManager::LoadSceneAsync("Level2", LoadSceneMode::Additive);
  r.frames(3);
  CHECK(op.isDone());
  CHECK_EQ(r.world.objectCount(), 3u);
  CHECK(!SceneManager::GetSceneByName("Level2").isLoaded());
  // a pool that is too small is named
  Rig q(32, 4);  // ScProbe pool: 3 + spare... capacity capped at 4
  addThreeScenes(q);
  q.finish(0);
  q.frames(1);
  char why[96];
  CHECK(!q.world.sceneFits(q.view, 2, why, sizeof(why)));
  CHECK(strstr(why, "ScProbe") != nullptr);
}

TEST(scene_pools_grow_for_scenes_flagged_additive) {
  Rig r(32, 32);
  r.b.addScene("Level1", {probe("Hero"), probe("Enemy"), probe("Coin")});
  r.b.addScene("Hud", {probe("Score"), probe("Life")}, true);
  r.finish(0);
  r.frames(1);
  const ComponentPool* p = r.world.pool<ScProbe>();
  CHECK(p != nullptr && p->capacity >= 5 + 8);  // 3 + 2 + spare
}

TEST(scene_async_failed_load_leaves_no_half_scene) {
  Rig r;
  addThreeScenes(r);
  r.finish(0);
  r.frames(1);
  SceneManager::setAsyncChunk(1);
  AsyncOperation op = SceneManager::LoadSceneAsync("Level2", LoadSceneMode::Additive);
  r.frames(3);  // part built
  SceneManager::LoadScene("Hud");  // a Single load destroys the unfinished scene with the rest
  r.frames(2);
  CHECK(op.isDone());
  CHECK_EQ(r.world.objectCount(), 2u);
  CHECK(!SceneManager::GetSceneByName("Level2").isLoaded());
  SceneManager::setAsyncChunk(32);
}

TEST(scene_pool_reserve_adds_slots_for_instantiate) {
  Rig r(256, 256);
  r.b.addScene("Main", {probe("Spawner"), probe("Template")});
  r.b.reserve.push_back({ScProbe::staticTypeInfo()->hash, 40});
  r.finish(0);
  r.frames(1);
  const ComponentPool* p = r.world.pool<ScProbe>();
  CHECK(p != nullptr && p->capacity >= 2 + 40 + 8);
  GameObject* proto = GameObject::Find("Template");
  int made = 0;
  for (int i = 0; i < 40; ++i)
    if (Instantiate(proto)) ++made;
  CHECK_EQ(made, 40);  // without the reserve only 8 would fit
}
