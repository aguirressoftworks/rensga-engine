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
  PackConsoleTarget target = {};
  bool hasTarget = false;
  u32 objectCount = 0;
  u32 componentCount = 0;
};

class World {
 public:
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

  // ---- lifecycle (one call each per frame, driven by GameLoop)
  void startPending();   // Awake + OnEnable for new behaviours, then Start
  void fixedUpdate();
  void update();
  void lateUpdate();
  void flushDestroyed(); // OnDisable/OnDestroy, free slots
  void setObjectActive(GameObject* go, bool active);
  void refreshEnabled(GameObject* go, bool recursive);
  // Calls OnTriggerEnter on every behaviour attached to `self`.
  void sendTriggerEnter(GameObject* self, GameObject* other);

  // ---- pack loading (MVP-27)
  bool loadPack(const PackView& pack, LoadedPack* out);

  Arena& arena() { return *arena_; }

 private:
  friend class Component;
  ComponentPool* poolFor(u32 typeId) const;
  ComponentPool* ensurePool(const TypeInfo& type, u32 capacity);
  void releaseObject(GameObject* go);
  bool shouldBeEnabled(const GameObject* go, const Component* c) const;
  template <class F>
  void forEachBehaviour(F&& f);

  WorldConfig config_;
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
T* GameObject::AddComponent() {
  return world->addComponent<T>(this);
}
inline Ref<GameObject> GameObject::ref() { return world->ref(this); }
inline void GameObject::SetActive(bool active) { world->setObjectActive(this, active); }

}  // namespace rensga
