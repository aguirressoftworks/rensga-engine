#include "rensga/core/world.h"

#include <string.h>

#include "rensga/core/components.h"
#include "rensga/core/log.h"

namespace rensga {

// ---- Transform / Component -------------------------------------------------

void Transform::setParent(Transform* newParent) {
  if (parent == newParent) return;
  if (parent) {
    Transform** link = &parent->firstChild;
    while (*link && *link != this) link = &(*link)->nextSibling;
    if (*link) *link = nextSibling;
  }
  nextSibling = nullptr;
  parent = newParent;
  if (newParent) {
    // Append to keep a stable child order.
    Transform** link = &newParent->firstChild;
    while (*link) link = &(*link)->nextSibling;
    *link = this;
  }
}

void Component::setEnabled(bool e) {
  if (enabled_ == e) return;
  enabled_ = e;
  if (world_) {
    GameObject* go = gameObject();
    if (go) world_->refreshEnabled(go, false);
  }
}

// ---- World: setup ----------------------------------------------------------

bool World::init(const WorldConfig& config, Arena& arena) {
  registerBuiltinComponents();
  config_ = config;
  arena_ = &arena;
  objects_ = arena.allocateArray<GameObject>(config.maxObjects);
  freeObjects_ = arena.allocateArray<u32>(config.maxObjects);
  if (!objects_ || !freeObjects_) return false;
  for (u32 i = 0; i < config.maxObjects; ++i) new (&objects_[i]) GameObject();
  freeObjectCount_ = 0;
  objectHighWater_ = 0;
  liveObjects_ = 0;
  poolCount_ = 0;
  for (u32 i = 0; i < kMaxTypes; ++i) pools_[i] = ComponentPool();
  pendingDestroy_ = false;
  return true;
}

void World::shutdown() {
  for (u32 i = 0; i < objectHighWater_; ++i)
    if (objects_[i].alive && !objects_[i].dying && !objects_[i].transform.parent) destroy(&objects_[i]);
  flushDestroyed();
}

GameObject* World::objectAt(u32 index) const {
  if (index >= objectHighWater_) return nullptr;
  return objects_[index].alive ? &objects_[index] : nullptr;
}

GameObject* World::findByName(const char* name) const {
  for (u32 i = 0; i < objectHighWater_; ++i)
    if (objects_[i].alive && !objects_[i].dying && strcmp(objects_[i].name, name) == 0) return &objects_[i];
  return nullptr;
}

GameObject* World::findByGuid(const Guid& guid) const {
  for (u32 i = 0; i < objectHighWater_; ++i)
    if (objects_[i].alive && !objects_[i].dying && objects_[i].guid == guid) return &objects_[i];
  return nullptr;
}

GameObject* World::createObject(const char* name, GameObject* parent) {
  u32 idx;
  if (freeObjectCount_ > 0) {
    idx = freeObjects_[--freeObjectCount_];
  } else if (objectHighWater_ < config_.maxObjects) {
    idx = objectHighWater_++;
  } else {
    RLOG_ERROR("world", "object capacity (%u) exhausted creating '%s'", config_.maxObjects, name ? name : "");
    return nullptr;
  }
  GameObject* go = &objects_[idx];
  u32 gen = go->generation;  // preserved across reuse so stale Refs stay null
  *go = GameObject();
  go->generation = gen;
  go->world = this;
  go->index = idx;
  go->alive = true;
  go->transform.gameObject = go;
  if (name) {
    strncpy(go->name, name, kMaxNameLength - 1);
    go->name[kMaxNameLength - 1] = 0;
  }
  if (parent) go->transform.setParent(&parent->transform);
  ++liveObjects_;
  return go;
}

// ---- components ------------------------------------------------------------

ComponentPool* World::poolFor(u32 typeId) const {
  if (typeId >= kMaxTypes) return nullptr;
  const ComponentPool* p = &pools_[typeId];
  return p->type ? const_cast<ComponentPool*>(p) : nullptr;
}

const ComponentPool* World::pool(const TypeInfo& type) const { return poolFor(type.id); }

ComponentPool* World::ensurePool(const TypeInfo& type, u32 capacity) {
  ComponentPool& p = pools_[type.id];
  if (p.type) return &p;
  if (capacity == 0) capacity = 1;
  p.type = &type;
  p.stride = static_cast<u32>(alignUp(type.size, kDmaAlignment));
  p.capacity = capacity;
  size_t align = type.align > kDmaAlignment ? type.align : kDmaAlignment;
  p.storage = static_cast<u8*>(arena_->allocate(static_cast<size_t>(p.stride) * capacity, align));
  p.generations = arena_->allocateArray<u32>(capacity);
  p.flags = arena_->allocateArray<u8>(capacity);
  p.freeList = arena_->allocateArray<u32>(capacity);
  if (!p.storage || !p.generations || !p.flags || !p.freeList) {
    p = ComponentPool();
    return nullptr;
  }
  for (u32 i = 0; i < capacity; ++i) {
    p.generations[i] = 1;
    p.flags[i] = 0;
  }
  poolOrder_[poolCount_++] = type.id;
  return &p;
}

bool World::reservePool(const TypeInfo& type, u32 capacity) {
  if (capacity > config_.maxComponentsPerType) {
    RLOG_ERROR("world", "component type '%s' needs %u slots, capacity is %u", type.name, capacity,
               config_.maxComponentsPerType);
    return false;
  }
  return ensurePool(type, capacity) != nullptr;
}

Component* World::addComponent(GameObject* go, const TypeInfo& type) {
  if (!go || !type.asComponent) return nullptr;
  if (go->componentCount >= kMaxComponentsPerObject) {
    RLOG_ERROR("world", "object '%s' has too many components", go->name);
    return nullptr;
  }
  ComponentPool* p = ensurePool(type, config_.maxComponentsPerType);
  if (!p) return nullptr;
  u32 slot;
  if (p->freeCount > 0) {
    slot = p->freeList[--p->freeCount];
  } else if (p->highWater < p->capacity) {
    slot = p->highWater++;
  } else {
    RLOG_ERROR("world", "component pool '%s' is full (%u)", type.name, p->capacity);
    return nullptr;
  }
  void* mem = p->at(slot);
  type.construct(mem);
  Component* c = type.asComponent(mem);
  c->world_ = this;
  c->ownerIndex_ = go->index;
  c->slot_ = slot;
  c->typeId_ = type.id;
  c->enabled_ = true;
  p->flags[slot] = kSlotAlive;
  ++p->liveCount;
  go->components[go->componentCount].typeId = type.id;
  go->components[go->componentCount].slot = slot;
  ++go->componentCount;
  return c;
}

Component* World::getComponent(const GameObject* go, const TypeInfo& type) const {
  if (!go) return nullptr;
  const ComponentPool* p = poolFor(type.id);
  if (!p) return nullptr;
  for (u32 i = 0; i < go->componentCount; ++i) {
    if (go->components[i].typeId != type.id) continue;
    u32 slot = go->components[i].slot;
    if ((p->flags[slot] & (kSlotAlive | kSlotDying)) == kSlotAlive) return type.asComponent(p->at(slot));
  }
  return nullptr;
}

// ---- destruction -----------------------------------------------------------

void World::destroy(GameObject* go) {
  if (!go || !go->alive || go->dying) return;
  go->dying = true;
  ++go->generation;  // Refs read null from now on
  pendingDestroy_ = true;
  for (u32 i = 0; i < go->componentCount; ++i) {
    ComponentPool* p = poolFor(go->components[i].typeId);
    u32 slot = go->components[i].slot;
    if (p && (p->flags[slot] & kSlotAlive) && !(p->flags[slot] & kSlotDying)) {
      p->flags[slot] |= kSlotDying;
      ++p->generations[slot];
    }
  }
  for (Transform* c = go->transform.firstChild; c; c = c->nextSibling) destroy(c->gameObject);
}

void World::destroy(Component* c) {
  if (!c) return;
  ComponentPool* p = poolFor(c->typeId_);
  if (!p || (p->flags[c->slot_] & kSlotDying)) return;
  p->flags[c->slot_] |= kSlotDying;
  ++p->generations[c->slot_];
  pendingDestroy_ = true;
}

bool World::shouldBeEnabled(const GameObject* go, const Component* c) const {
  return go->activeInHierarchy() && c->enabled() && !go->dying;
}

void World::releaseObject(GameObject* go) {
  // Components first: OnDisable then OnDestroy, then destruct and free the slot.
  for (u32 i = 0; i < go->componentCount; ++i) {
    ComponentPool* p = poolFor(go->components[i].typeId);
    u32 slot = go->components[i].slot;
    if (!p || !(p->flags[slot] & kSlotAlive)) continue;
    void* mem = p->at(slot);
    if (Behaviour* b = p->type->asBehaviour ? p->type->asBehaviour(mem) : nullptr) {
      if (p->flags[slot] & kSlotEnabledNotified) b->OnDisable();
      if (p->flags[slot] & kSlotAwoken) b->OnDestroy();
    }
    p->type->destruct(mem);
    p->flags[slot] = 0;
    p->freeList[p->freeCount++] = slot;
    --p->liveCount;
  }
  go->componentCount = 0;
  go->transform.setParent(nullptr);
  go->alive = false;
  go->dying = false;
  // Detach orphan children pointers (children were destroyed too, release them in this pass).
  --liveObjects_;
  freeObjects_[freeObjectCount_++] = go->index;
}

void World::flushDestroyed() {
  if (!pendingDestroy_) return;
  pendingDestroy_ = false;
  // Whole objects. Children release first because they are also flagged dying.
  for (u32 i = 0; i < objectHighWater_; ++i) {
    GameObject* go = &objects_[i];
    if (go->alive && go->dying) {
      // Make sure the transform list of a dying parent does not keep a dying child.
      releaseObject(go);
    }
  }
  // Individually destroyed components on surviving objects.
  for (u32 pi = 0; pi < poolCount_; ++pi) {
    ComponentPool& p = pools_[poolOrder_[pi]];
    for (u32 slot = 0; slot < p.highWater; ++slot) {
      if ((p.flags[slot] & (kSlotAlive | kSlotDying)) != (kSlotAlive | kSlotDying)) continue;
      void* mem = p.at(slot);
      Component* c = p.type->asComponent(mem);
      GameObject* go = objectAt(c->ownerIndex_);
      if (Behaviour* b = p.type->asBehaviour ? p.type->asBehaviour(mem) : nullptr) {
        if (p.flags[slot] & kSlotEnabledNotified) b->OnDisable();
        if (p.flags[slot] & kSlotAwoken) b->OnDestroy();
      }
      if (go) {
        for (u32 k = 0; k < go->componentCount; ++k) {
          if (go->components[k].typeId == p.type->id && go->components[k].slot == slot) {
            go->components[k] = go->components[--go->componentCount];
            break;
          }
        }
      }
      p.type->destruct(mem);
      p.flags[slot] = 0;
      p.freeList[p.freeCount++] = slot;
      --p.liveCount;
    }
  }
}

// ---- lifecycle -------------------------------------------------------------

template <class F>
void World::forEachBehaviour(F&& f) {
  for (u32 pi = 0; pi < poolCount_; ++pi) {
    ComponentPool& p = pools_[poolOrder_[pi]];
    if (!p.type->asBehaviour) continue;
    for (u32 slot = 0; slot < p.highWater; ++slot) {  // highWater re-read: new slots may appear
      if ((p.flags[slot] & (kSlotAlive | kSlotDying)) != kSlotAlive) continue;
      Behaviour* b = p.type->asBehaviour(p.at(slot));
      f(p, slot, b);
    }
  }
}

void World::refreshEnabled(GameObject* go, bool recursive) {
  for (u32 i = 0; i < go->componentCount; ++i) {
    ComponentPool* p = poolFor(go->components[i].typeId);
    u32 slot = go->components[i].slot;
    if (!p || !p->type->asBehaviour) continue;
    if ((p->flags[slot] & (kSlotAlive | kSlotDying | kSlotAwoken)) != (kSlotAlive | kSlotAwoken)) continue;
    Behaviour* b = p->type->asBehaviour(p->at(slot));
    bool should = shouldBeEnabled(go, b);
    bool notified = (p->flags[slot] & kSlotEnabledNotified) != 0;
    if (should && !notified) {
      p->flags[slot] |= kSlotEnabledNotified;
      b->OnEnable();
    } else if (!should && notified) {
      p->flags[slot] &= static_cast<u8>(~kSlotEnabledNotified);
      b->OnDisable();
    }
  }
  if (recursive)
    for (Transform* c = go->transform.firstChild; c; c = c->nextSibling) refreshEnabled(c->gameObject, true);
}

void World::setObjectActive(GameObject* go, bool active) {
  if (!go || go->activeSelf == active) return;
  go->activeSelf = active;
  refreshEnabled(go, true);
}

void World::startPending() {
  for (int round = 0; round < 8; ++round) {
    bool worked = false;
    // Pass 1: Awake, then OnEnable.
    forEachBehaviour([&](ComponentPool& p, u32 slot, Behaviour* b) {
      GameObject* go = objectAt(b->ownerIndex_);
      if ((p.flags[slot] & kSlotAwoken) || !go || !go->activeInHierarchy()) return;
      p.flags[slot] |= kSlotAwoken;
      worked = true;
      b->Awake();
      if (shouldBeEnabled(go, b) && (p.flags[slot] & (kSlotAlive | kSlotDying)) == kSlotAlive) {
        p.flags[slot] |= kSlotEnabledNotified;
        b->OnEnable();
      }
    });
    // Pass 2: Start, once, before the first Update.
    forEachBehaviour([&](ComponentPool& p, u32 slot, Behaviour* b) {
      GameObject* go = objectAt(b->ownerIndex_);
      if (!(p.flags[slot] & kSlotAwoken) || (p.flags[slot] & kSlotStarted) || !go) return;
      if (!shouldBeEnabled(go, b)) return;
      p.flags[slot] |= kSlotStarted;
      worked = true;
      b->Start();
    });
    if (!worked) break;
  }
}

#define RENSGA_RUN_PHASE(call)                                                                 \
  forEachBehaviour([&](ComponentPool& p, u32 slot, Behaviour* b) {                             \
    if (!(p.flags[slot] & kSlotStarted)) return;                                               \
    GameObject* go = objectAt(b->ownerIndex_);                                                 \
    if (!go || !shouldBeEnabled(go, b)) return;                                                \
    b->call();                                                                                 \
  })

void World::fixedUpdate() { RENSGA_RUN_PHASE(FixedUpdate); }
void World::update() { RENSGA_RUN_PHASE(Update); }
void World::lateUpdate() { RENSGA_RUN_PHASE(LateUpdate); }

void World::sendTriggerEnter(GameObject* self, GameObject* other) {
  if (!self || self->dying) return;
  for (u32 i = 0; i < self->componentCount; ++i) {
    ComponentPool* p = poolFor(self->components[i].typeId);
    u32 slot = self->components[i].slot;
    if (!p || !p->type->asBehaviour) continue;
    if ((p->flags[slot] & (kSlotAlive | kSlotDying | kSlotStarted)) != (kSlotAlive | kSlotStarted)) continue;
    p->type->asBehaviour(p->at(slot))->OnTriggerEnter(other);
  }
}

}  // namespace rensga
