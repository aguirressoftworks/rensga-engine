// MVP-12: World owns objects and fixed-capacity, contiguous component pools (one per type).
#pragma once
#include "rensga/core/memory.h"
#include "rensga/core/object.h"
#include "rensga/core/pack_format.h"
#include "rensga/core/backends.h"

namespace rensga {

struct WorldConfig {
  u32 maxObjects = 256;
  u32 maxComponentsPerType = 256;
  u32 spareComponents = 8;  // extra slots per loaded type for runtime Instantiate
  u32 maxDelayedDestroys = 128;  // pending Destroy(obj, delay) entries
  u32 maxCoroutines = kDefaultMaxCoroutines;  // live coroutines (ConsoleTarget budgets.maxCoroutines)
  u32 maxInvokes = kDefaultMaxInvokes;        // pending Invoke / InvokeRepeating timers
};

enum PoolFlags : u8 {
  kSlotAlive = 1,
  kSlotDying = 2,
  kSlotAwoken = 4,
  kSlotStarted = 8,
  kSlotEnabledNotified = 16,
};

struct ComponentPool {
  const TypeInfo* type = nullptr;
  u8* storage = nullptr;
  u32 stride = 0;
  u32 capacity = 0;
  u32 highWater = 0;   // slots [0, highWater) have been used at least once
  u32 liveCount = 0;
  u32* generations = nullptr;
  u8* flags = nullptr;
  u32* freeList = nullptr;
  u32 freeCount = 0;
  void* at(u32 slot) const { return storage + static_cast<size_t>(slot) * stride; }
};

// What a loaded pack provides beyond objects/components.
struct LoadedPack {
  MeshTable meshes;
  const PackInputAction* inputs = nullptr;
  u32 inputCount = 0;
  PackAssets assets;  // v2 textures, audio, tilemaps, clips, fonts (empty for v1 packs)
  PackConsoleTarget target = {};
  bool hasTarget = false;
  u32 objectCount = 0;
  u32 componentCount = 0;
};

// One pending Destroy(obj, delay). typeId == kInvalidIndex means a GameObject, otherwise a component slot.
struct DelayedDestroy {
  u32 typeId = kInvalidIndex;
  u32 index = 0;
  u32 generation = 0;
  Scalar remaining = 0;
};

class World {
 public:
  // The World the static lookups (GameObject::Find, Instantiate without an explicit owner) use: the most
  // recently initialised one that has not been shut down.
  static World* active() { return active_; }

  bool init(const WorldConfig& config, Arena& arena);
  // Runs OnDisable/OnDestroy for everything still alive.
  void shutdown();

  // ---- objects
  GameObject* createObject(const char* name, GameObject* parent = nullptr);
  void destroy(GameObject* go);   // Refs read null immediately; callbacks run at flushDestroyed()
  void destroy(Component* c);
  u32 objectCount() const { return liveObjects_; }
  u32 objectCapacity() const { return config_.maxObjects; }
  u32 objectHighWater() const { return objectHighWater_; }
  GameObject* objectAt(u32 index) const;  // null if not alive
  GameObject* findByName(const char* name) const;
  GameObject* findByGuid(const Guid& guid) const;
  GameObject* findByTag(const char* tag) const;
  // Fills `out` with up to `capacity` live objects carrying `tag` (object index order); returns the count found.
  u32 findAllByTag(const char* tag, GameObject** out, u32 capacity) const;

  // ---- Unity Instantiate / Destroy
  // Clones `original` and its children (reflected fields are copied, so the clone is a prefab-like copy).
  // The clone is named "<name> (Clone)". With pos/rot the root is placed in world space. Awake/OnEnable run
  // immediately once the world has started; Start runs before its first Update. Null if a pool is full.
  GameObject* instantiate(const GameObject* original, Transform* parent = nullptr, const Vector3* pos = nullptr,
                          const Quaternion* rot = nullptr, bool keepLocalUnderParent = true);
  // Destroy(obj, delay): Refs read null once the delay passed; callbacks run at the end of that frame.
  void destroy(GameObject* go, Scalar delay);
  void destroy(Component* c, Scalar delay);
  // Engine/editor only (not exposed to scripts): destroys and runs OnDisable/OnDestroy right now.
  void destroyImmediate(GameObject* go);

  // ---- components
  bool reservePool(const TypeInfo& type, u32 capacity);
  Component* addComponent(GameObject* go, const TypeInfo& type);
  Component* getComponent(const GameObject* go, const TypeInfo& type) const;
  template <class T>
  T* addComponent(GameObject* go) {
    return static_cast<T*>(addComponent(go, *T::staticTypeInfo()));
  }
  template <class T>
  T* getComponent(const GameObject* go) const {
    return static_cast<T*>(getComponent(go, *T::staticTypeInfo()));
  }
  // The n-th (0-based) live component of `type` on `go`, or null.
  Component* getComponentNth(const GameObject* go, const TypeInfo& type, u32 n) const;
  Component* getComponentInChildren(const GameObject* go, const TypeInfo& type, bool includeInactive) const;
  Component* getComponentInParent(const GameObject* go, const TypeInfo& type) const;
  const ComponentPool* pool(const TypeInfo& type) const;
  template <class T>
  const ComponentPool* pool() const {
    return pool(*T::staticTypeInfo());
  }
  // Calls f(T&) for every live (not destroyed) component of type T, in slot order.
  template <class T, class F>
  void forEach(F&& f) {
    const ComponentPool* p = pool<T>();
    if (!p) return;
    for (u32 i = 0; i < p->highWater; ++i)
      if ((p->flags[i] & (kSlotAlive | kSlotDying)) == kSlotAlive) f(*static_cast<T*>(p->at(i)));
  }

  template <class T>
  Ref<T> ref(T* object);
  template <class T>
  T* resolve(u32 index, u32 generation) const;

  // Editor edit mode (Unity ExecuteInEditMode): while on, only behaviours of types declared EXECUTE_IN_EDIT_MODE() take part in
  // Awake / OnEnable / Start / Update / LateUpdate / FixedUpdate. Everything else stays dormant.
  void setEditMode(bool on) { editMode_ = on; }
  bool editMode() const { return editMode_; }
  // Editor: calls f(ctx, behaviour, owner, enabled) for every live behaviour, running or not (gizmo callbacks). No heap.
  void visitBehaviours(void (*f)(void* ctx, Behaviour* b, GameObject* owner, bool enabled), void* ctx);

  // ---- lifecycle (one call each per frame, driven by GameLoop)
  void startPending();   // Awake + OnEnable for new behaviours, then Start
  void fixedUpdate();
  void update();
  void lateUpdate();
  void flushDestroyed(); // delayed-destroy timers, then OnDisable/OnDestroy, free slots
  void setObjectActive(GameObject* go, bool active);
  // Awake + OnEnable for every not yet awake component of `go` and its children (no-op until started).
  void awakeSubtree(GameObject* go);
  void refreshEnabled(GameObject* go, bool recursive);
  // Calls OnTriggerEnter on every behaviour attached to `self`.
  void sendTriggerEnter(GameObject* self, GameObject* other);
  // Calls the matching contact callback (Behaviour::OnTriggerExit, OnTriggerEnter2D, ...) on every behaviour of `self`.
  enum class Contact : u8 { TriggerEnter, TriggerExit, TriggerEnter2D, TriggerExit2D, CollisionEnter2D, CollisionExit2D,
                            TriggerStay, TriggerStay2D };  // F6 (A2): the Stay kinds were appended
  void sendContact(GameObject* self, GameObject* other, Contact kind);
  // --- F6 a2 ---  3D / 2D collision callbacks with their data (physics3d dispatches these after the step).
  enum class CollisionKind : u8 { Enter, Stay, Exit };
  void sendCollision(GameObject* self, const Collision& c, CollisionKind kind);
  void sendCollision2D(GameObject* self, const Collision2D& c, CollisionKind kind);
  // --- end F6 a2 ---
  // F5.13: calls OnApplicationPause(paused) on every started behaviour (pumpApplicationEvents calls this).
  void sendApplicationPause(bool paused);
  // Calls OnUIClick on every behaviour attached to `self`.
  void sendUIClick(GameObject* self);

  // ---- pack loading (MVP-27)
  // `sceneIndex` picks one scene of a multi-scene pack (SCNE section); a pack without SCNE has one scene.
  // After the world started, the loaded objects wake up (Awake/OnEnable) before this returns.
  bool loadPack(const PackView& pack, LoadedPack* out, u32 sceneIndex = 0);
  // Destroys every object now (scene change). Pools keep their capacity. With keepDontDestroy, root objects
  // marked by dontDestroyOnLoad (and their children) survive.
  void destroyAllImmediate(bool keepDontDestroy = false);
  // Unity's DontDestroyOnLoad: a child is first moved to the scene root (world position kept).
  void dontDestroyOnLoad(GameObject* go);

  Arena& arena() { return *arena_; }

  // ---- F6 (A3): additive and incremental scene loading (engine/core/src/scene_load.cpp). Objects carry the SCNE index of the
  // scene that made them (GameObject::sceneIndex). A load builds the scene's objects INACTIVE and not awake over several
  // steps (stepSceneLoad), then finishSceneLoad restores their active flags and wakes the roots like loadPack does.
  struct SceneLoadJob {
    const PackView* pack = nullptr;
    u32 scene = 0, first = 0, count = 0;
    u32 phase = 0;    // 0 objects, 1 parents, 2 components, 3 mesh links, 4 built
    u32 cursor = 0;
    u32 done = 0, total = 1;  // work items finished / in all (progress = done / total)
    bool active = false;
  };
  // Does the scene fit next to what is loaded: free object slots, free slots of each component pool, arena bytes for new pools
  // and the job table. False fills `why` (names the pool or the figure). No allocation.
  bool sceneFits(const PackView& pack, u32 sceneIndex, char* why, u32 whySize) const;
  bool beginSceneLoad(const PackView& pack, u32 sceneIndex, SceneLoadJob& job, char* why, u32 whySize);
  // Builds up to `maxItems` more objects / links / components. True once the whole scene is built.
  bool stepSceneLoad(SceneLoadJob& job, u32 maxItems);
  bool finishSceneLoad(SceneLoadJob& job);  // activates and wakes the built scene
  void abortSceneLoad(SceneLoadJob& job);   // destroys what was built
  u32 destroyScene(i32 sceneIndex);         // root objects (with children) made by that scene, DontDestroyOnLoad roots excepted
  u32 sceneObjectCount(i32 sceneIndex) const;

  // ---- coroutines, Invoke (F3.05): the loop resumes them at Unity's points; see coroutine.h
  Scheduler& scheduler() { return scheduler_; }
  // Handle to a live component of this world (false for null or a component of another world).
  bool handleOf(const Component* c, ComponentHandle& out) const;
  // The component a handle names, or null once it is destroyed or dying.
  Component* resolveHandle(const ComponentHandle& h) const;

 private:
  friend class Component;
  ComponentPool* poolFor(u32 typeId) const;
  ComponentPool* ensurePool(const TypeInfo& type, u32 capacity);
  void releaseObject(GameObject* go);
  void flushNow();
  void tickDelayed(Scalar dt);
  bool addDelayed(const DelayedDestroy& d);
  void awakeComponent(ComponentPool& p, u32 slot, GameObject* go);
  GameObject* cloneSubtree(const GameObject* src, Transform* parent, bool root);
  Component* findInChildren(const GameObject* go, const TypeInfo& type, bool includeInactive) const;
  bool shouldBeEnabled(const GameObject* go, const Component* c) const;
  template <class F>
  void forEachBehaviour(F&& f);

  WorldConfig config_;
  Scheduler scheduler_;
  Arena* arena_ = nullptr;
  GameObject* objects_ = nullptr;
  u32* freeObjects_ = nullptr;
  u32 freeObjectCount_ = 0;
  u32 objectHighWater_ = 0;
  u32 liveObjects_ = 0;
  ComponentPool pools_[kMaxTypes];
  u32 poolOrder_[kMaxTypes] = {};
  u32 poolCount_ = 0;
  bool pendingDestroy_ = false;
  bool editMode_ = false;
  bool started_ = false;      // true after the first startPending(): later components Awake immediately
  u32 cloneDepth_ = 0;        // >0 while Instantiate copies: the clone wakes up once it is complete
  u32* sceneMap_ = nullptr;   // F6 (A3): scene-local index -> object index while an incremental load runs (one per world)
  DelayedDestroy* delayed_ = nullptr;
  u32 delayedCount_ = 0;
  static inline World* active_ = nullptr;
};

// ---- inline definitions needing a complete World -----------------------------

template <class T>
Ref<T> World::ref(T* object) {
  if (!object) return Ref<T>();
  if constexpr (__is_same(T, GameObject)) {
    if (object->dying || !object->alive) return Ref<T>();
    return Ref<T>(this, object->index, object->generation);
  } else {
    const ComponentPool* p = poolFor(T::staticTypeInfo()->id);
    Component* c = object;
    u32 slot = c->slot_;
    if (!p || slot >= p->highWater || (p->flags[slot] & kSlotDying) || !(p->flags[slot] & kSlotAlive)) return Ref<T>();
    return Ref<T>(this, slot, p->generations[slot]);
  }
}

template <class T>
T* World::resolve(u32 index, u32 generation) const {
  if constexpr (__is_same(T, GameObject)) {
    if (index >= objectHighWater_) return nullptr;
    GameObject* go = &objects_[index];
    return (go->alive && go->generation == generation) ? go : nullptr;
  } else {
    const ComponentPool* p = poolFor(T::staticTypeInfo()->id);
    if (!p || index >= p->highWater) return nullptr;
    if (!(p->flags[index] & kSlotAlive) || p->generations[index] != generation) return nullptr;
    return static_cast<T*>(p->at(index));
  }
}

template <class T>
T* Ref<T>::get() const {
  return world_ ? world_->template resolve<T>(index_, generation_) : nullptr;
}

inline GameObject* Component::gameObject() const { return world_->objectAt(ownerIndex_); }
inline Transform* Component::transform() const { return &gameObject()->transform; }

template <class T>
T* GameObject::GetComponent() const {
  return world->getComponent<T>(this);
}
template <class T>
u32 GameObject::GetComponents(T** out, u32 capacity) const {
  u32 n = 0;
  while (n < capacity) {
    Component* c = world->getComponentNth(this, *T::staticTypeInfo(), n);
    if (!c) break;
    out[n++] = static_cast<T*>(c);
  }
  return n;
}
template <class T>
T* GameObject::GetComponentInChildren(bool includeInactive) const {
  return static_cast<T*>(world->getComponentInChildren(this, *T::staticTypeInfo(), includeInactive));
}
template <class T>
T* GameObject::GetComponentInParent() const {
  return static_cast<T*>(world->getComponentInParent(this, *T::staticTypeInfo()));
}
template <class T>
T* GameObject::AddComponent() {
  return world->addComponent<T>(this);
}
inline Ref<GameObject> GameObject::ref() { return world->ref(this); }
inline void GameObject::SetActive(bool active) { world->setObjectActive(this, active); }
inline GameObject* GameObject::Find(const char* objectName) {
  return World::active() ? World::active()->findByName(objectName) : nullptr;
}
inline GameObject* GameObject::FindWithTag(const char* objectTag) {
  return World::active() ? World::active()->findByTag(objectTag) : nullptr;
}
inline u32 GameObject::FindGameObjectsWithTag(const char* objectTag, GameObject** out, u32 capacity) {
  return World::active() ? World::active()->findAllByTag(objectTag, out, capacity) : 0;
}

// Unity's Object.FindObjectOfType<T>(): the first live component of type T on an active object (slot order) in the
// active World, or null. includeInactive also accepts components of inactive objects. No heap.
template <class T>
T* FindObjectOfType(bool includeInactive = false) {
  World* w = World::active();
  const ComponentPool* p = w ? w->pool<T>() : nullptr;
  if (!p) return nullptr;
  for (u32 i = 0; i < p->highWater; ++i) {
    if ((p->flags[i] & (kSlotAlive | kSlotDying)) != kSlotAlive) continue;
    T* c = static_cast<T*>(p->at(i));
    GameObject* go = c->gameObject();
    if (go && (includeInactive || go->activeInHierarchy())) return c;
  }
  return nullptr;
}
// Fills `out` (up to `capacity`) with every such component; returns the count found.
template <class T>
u32 FindObjectsOfType(T** out, u32 capacity, bool includeInactive = false) {
  World* w = World::active();
  const ComponentPool* p = w ? w->pool<T>() : nullptr;
  u32 n = 0;
  if (!p) return 0;
  for (u32 i = 0; i < p->highWater && n < capacity; ++i) {
    if ((p->flags[i] & (kSlotAlive | kSlotDying)) != kSlotAlive) continue;
    T* c = static_cast<T*>(p->at(i));
    GameObject* go = c->gameObject();
    if (go && (includeInactive || go->activeInHierarchy())) out[n++] = c;
  }
  return n;
}

}  // namespace rensga
