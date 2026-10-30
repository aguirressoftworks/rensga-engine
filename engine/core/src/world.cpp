#include "rensga/core/world.h"

#include <string.h>

#include "rensga/core/components.h"
#include "rensga/core/debug.h"
#include "rensga/core/log.h"
#include "rensga/core/time.h"

namespace rensga {

// ---- Transform / Component -------------------------------------------------

void Transform::setParent(Transform* newParent) {
  if (parent_ == newParent) return;
  if (parent_) {
    Transform** link = &parent_->firstChild_;
    while (*link && *link != this) link = &(*link)->nextSibling_;
    if (*link) *link = nextSibling_;
  }
  nextSibling_ = nullptr;
  parent_ = newParent;
  if (newParent) {
    // Append to keep a stable child order.
    Transform** link = &newParent->firstChild_;
    while (*link) link = &(*link)->nextSibling_;
    *link = this;
  }
}

u32 Transform::childCount() const {
  u32 n = 0;
  for (const Transform* c = firstChild_; c; c = c->nextSibling_) ++n;
  return n;
}

Transform* Transform::GetChild(u32 index) const {
  Transform* c = firstChild_;
  while (c && index--) c = c->nextSibling_;
  return c;
}

Transform* Transform::Find(const char* childName) const {
  if (!childName) return nullptr;
  const Transform* cur = this;
  const char* p = childName;
  while (*p && cur) {
    const char* end = p;
    while (*end && *end != '/') ++end;
    size_t len = static_cast<size_t>(end - p);
    const Transform* next = nullptr;
    for (const Transform* c = cur->firstChild_; c; c = c->nextSibling_) {
      if (strlen(c->gameObject->name) == len && strncmp(c->gameObject->name, p, len) == 0) {
        next = c;
        break;
      }
    }
    cur = next;
    p = *end == '/' ? end + 1 : end;
  }
  return const_cast<Transform*>(cur == this ? nullptr : cur);
}

void Transform::SetParent(Transform* newParent, bool worldPositionStays) {
  if (newParent == this || parent_ == newParent) return;
  if (newParent && newParent->IsChildOf(this)) {
    RLOG_ERROR("world", "SetParent: '%s' cannot become a child of its own descendant '%s'", gameObject->name,
               newParent->gameObject->name);
    return;
  }
  World* w = gameObject->world;
  bool wasActive = gameObject->activeInHierarchy();
  Vector3 wp = position();
  Quaternion wr = rotation();
  Vector3 ws = lossyScale();
  setParent(newParent);
  if (worldPositionStays) {
    setPosition(wp);
    setRotation(wr);
    Vector3 ps = newParent ? newParent->lossyScale() : Vector3(1, 1, 1);
    lscale_ = Vector3(ps.x != 0 ? ws.x / ps.x : ws.x, ps.y != 0 ? ws.y / ps.y : ws.y, ps.z != 0 ? ws.z / ps.z : ws.z);
  }
  if (w && wasActive != gameObject->activeInHierarchy()) {
    w->refreshEnabled(gameObject, true);
    if (!wasActive) w->awakeSubtree(gameObject);
  }
}

namespace {
// Rotation whose +Z axis is `forward` and whose +Y is as close to `up` as possible.
Quaternion lookRotation(const Vector3& forward, const Vector3& up) {
  Vector3 f = forward.normalized();
  Vector3 r = Vector3::Cross(up, f);
  if (r.sqrMagnitude() < 1e-12f) r = Vector3::Cross(Vector3(0, 0, 1), f);
  if (r.sqrMagnitude() < 1e-12f) r = Vector3::Cross(Vector3(1, 0, 0), f);
  r = r.normalized();
  Vector3 u = Vector3::Cross(f, r);
  // Rotation matrix with columns r, u, f -> quaternion (Shepperd).
  Scalar m00 = r.x, m01 = u.x, m02 = f.x;
  Scalar m10 = r.y, m11 = u.y, m12 = f.y;
  Scalar m20 = r.z, m21 = u.z, m22 = f.z;
  Scalar tr = m00 + m11 + m22;
  Quaternion q;
  if (tr > 0) {
    Scalar s = Mathf::Sqrt(tr + 1.0f) * 2.0f;
    q = Quaternion(Scalar(m21 - m12) / s, Scalar(m02 - m20) / s, Scalar(m10 - m01) / s, 0.25f * s);
  } else if (m00 > m11 && m00 > m22) {
    Scalar s = Mathf::Sqrt(1.0f + m00 - m11 - m22) * 2.0f;
    q = Quaternion(0.25f * s, Scalar(m01 + m10) / s, Scalar(m02 + m20) / s, Scalar(m21 - m12) / s);
  } else if (m11 > m22) {
    Scalar s = Mathf::Sqrt(1.0f + m11 - m00 - m22) * 2.0f;
    q = Quaternion(Scalar(m01 + m10) / s, 0.25f * s, Scalar(m12 + m21) / s, Scalar(m02 - m20) / s);
  } else {
    Scalar s = Mathf::Sqrt(1.0f + m22 - m00 - m11) * 2.0f;
    q = Quaternion(Scalar(m02 + m20) / s, Scalar(m12 + m21) / s, 0.25f * s, Scalar(m10 - m01) / s);
  }
  return q.normalized();
}
}  // namespace

void Transform::LookAt(const Vector3& target, const Vector3& worldUp) {
  Vector3 d = target - position();
  if (d.sqrMagnitude() < 1e-12f) return;
  setRotation(lookRotation(d, worldUp));
}

void GameObject::SetTag(const char* newTag) {
  size_t n = newTag ? strlen(newTag) : 0;
  if (n == 0) {
    newTag = "Untagged";
    n = 8;
  }
  if (n >= kMaxTagLength) n = kMaxTagLength - 1;
  memcpy(tag, newTag, n);
  tag[n] = 0;
}

bool GameObject::CompareTag(const char* other) const { return other && strcmp(tag, other) == 0; }

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
  started_ = false;
  cloneDepth_ = 0;
  delayedCount_ = 0;
  delayed_ = config.maxDelayedDestroys ? arena.allocateArray<DelayedDestroy>(config.maxDelayedDestroys) : nullptr;
  if (config.maxDelayedDestroys && !delayed_) return false;
  if (!scheduler_.init(*this, arena, config.maxCoroutines, config.maxInvokes)) return false;
  active_ = this;
  return true;
}

void World::shutdown() {
  for (u32 i = 0; i < objectHighWater_; ++i)
    if (objects_[i].alive && !objects_[i].dying && !objects_[i].transform.parent_) destroy(&objects_[i]);
  flushNow();
  delayedCount_ = 0;
  scheduler_.shutdown();
  if (active_ == this) active_ = nullptr;
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

GameObject* World::findByTag(const char* tag) const {
  if (!tag) return nullptr;
  for (u32 i = 0; i < objectHighWater_; ++i)
    if (objects_[i].alive && !objects_[i].dying && objects_[i].activeInHierarchy() && strcmp(objects_[i].tag, tag) == 0)
      return &objects_[i];
  return nullptr;
}

u32 World::findAllByTag(const char* tag, GameObject** out, u32 capacity) const {
  u32 n = 0;
  if (!tag) return 0;
  for (u32 i = 0; i < objectHighWater_ && n < capacity; ++i)
    if (objects_[i].alive && !objects_[i].dying && objects_[i].activeInHierarchy() && strcmp(objects_[i].tag, tag) == 0)
      out[n++] = &objects_[i];
  return n;
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
  // Keep poolOrder_ sorted by script execution order (lower first); equal orders keep creation order.
  u32 at = poolCount_;
  while (at > 0 && pools_[poolOrder_[at - 1]].type->executionOrder > type.executionOrder) {
    poolOrder_[at] = poolOrder_[at - 1];
    --at;
  }
  poolOrder_[at] = type.id;
  ++poolCount_;
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
  if (started_ && cloneDepth_ == 0) awakeComponent(*p, slot, go);
  return c;
}

Component* World::getComponentNth(const GameObject* go, const TypeInfo& type, u32 n) const {
  if (!go) return nullptr;
  const ComponentPool* p = poolFor(type.id);
  if (!p) return nullptr;
  for (u32 i = 0; i < go->componentCount; ++i) {
    if (go->components[i].typeId != type.id) continue;
    u32 slot = go->components[i].slot;
    if ((p->flags[slot] & (kSlotAlive | kSlotDying)) != kSlotAlive) continue;
    if (n-- == 0) return type.asComponent(p->at(slot));
  }
  return nullptr;
}

Component* World::findInChildren(const GameObject* go, const TypeInfo& type, bool includeInactive) const {
  if (!includeInactive && !go->selfActive_) return nullptr;
  if (Component* c = getComponent(go, type)) return c;
  for (const Transform* t = go->transform.firstChild_; t; t = t->nextSibling_)
    if (Component* c = findInChildren(t->gameObject, type, includeInactive)) return c;
  return nullptr;
}

Component* World::getComponentInChildren(const GameObject* go, const TypeInfo& type, bool includeInactive) const {
  if (!go) return nullptr;
  return findInChildren(go, type, includeInactive);
}

Component* World::getComponentInParent(const GameObject* go, const TypeInfo& type) const {
  for (const Transform* t = go ? &go->transform : nullptr; t; t = t->parent_)
    if (Component* c = getComponent(t->gameObject, type)) return c;
  return nullptr;
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
  for (Transform* c = go->transform.firstChild_; c; c = c->nextSibling_) destroy(c->gameObject);
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
  tickDelayed(Time::deltaTime());
  flushNow();
}

void World::flushNow() {
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
    if (editMode_ && !p.type->executeInEditMode) continue;
    for (u32 slot = 0; slot < p.highWater; ++slot) {  // highWater re-read: new slots may appear
      if ((p.flags[slot] & (kSlotAlive | kSlotDying)) != kSlotAlive) continue;
      Behaviour* b = p.type->asBehaviour(p.at(slot));
      f(p, slot, b);
    }
  }
}

void World::visitBehaviours(void (*f)(void*, Behaviour*, GameObject*, bool), void* ctx) {
  for (u32 pi = 0; pi < poolCount_; ++pi) {
    ComponentPool& p = pools_[poolOrder_[pi]];
    if (!p.type->asBehaviour) continue;
    for (u32 slot = 0; slot < p.highWater; ++slot) {
      if ((p.flags[slot] & (kSlotAlive | kSlotDying)) != kSlotAlive) continue;
      Behaviour* b = p.type->asBehaviour(p.at(slot));
      GameObject* go = objectAt(b->ownerIndex_);
      if (!go || go->dying) continue;
      f(ctx, b, go, go->activeInHierarchy() && b->enabled());
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
    for (Transform* c = go->transform.firstChild_; c; c = c->nextSibling_) refreshEnabled(c->gameObject, true);
}

void World::awakeComponent(ComponentPool& p, u32 slot, GameObject* go) {
  if (!p.type->asBehaviour) return;
  if (editMode_ && !p.type->executeInEditMode) return;
  if ((p.flags[slot] & (kSlotAlive | kSlotDying | kSlotAwoken)) != kSlotAlive || !go->activeInHierarchy()) return;
  Behaviour* b = p.type->asBehaviour(p.at(slot));
  p.flags[slot] |= kSlotAwoken;
  b->Awake();
  if (shouldBeEnabled(go, b) && (p.flags[slot] & (kSlotAlive | kSlotDying)) == kSlotAlive) {
    p.flags[slot] |= kSlotEnabledNotified;
    b->OnEnable();
  }
}

void World::awakeSubtree(GameObject* go) {
  if (!started_ || !go || !go->alive || go->dying || !go->activeInHierarchy()) return;
  for (u32 i = 0; i < go->componentCount; ++i) {
    ComponentPool* p = poolFor(go->components[i].typeId);
    if (p) awakeComponent(*p, go->components[i].slot, go);
  }
  for (Transform* c = go->transform.firstChild_; c; c = c->nextSibling_) awakeSubtree(c->gameObject);
}

void World::setObjectActive(GameObject* go, bool active) {
  if (!go || go->selfActive_ == active) return;
  go->selfActive_ = active;
  if (!active) scheduler_.onObjectDeactivated(go);  // coroutines and Invoke timers stop with the object
  refreshEnabled(go, true);
  if (active) awakeSubtree(go);  // first activation: Awake + OnEnable now, Start before the next Update
}

void World::startPending() {
  started_ = true;
  Debug::tickLines(Time::deltaTime());
  for (int round = 0; round < 8; ++round) {
    bool worked = false;
    // Pass 1: Awake, then OnEnable.
    forEachBehaviour([&](ComponentPool& p, u32 slot, Behaviour* b) {
      GameObject* go = objectAt(b->ownerIndex_);
      if ((p.flags[slot] & kSlotAwoken) || !go || !go->activeInHierarchy()) return;
      worked = true;
      awakeComponent(p, slot, go);
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

void World::sendTriggerEnter(GameObject* self, GameObject* other) { sendContact(self, other, Contact::TriggerEnter); }

void World::sendContact(GameObject* self, GameObject* other, Contact kind) {
  if (!self || self->dying) return;
  for (u32 i = 0; i < self->componentCount; ++i) {
    ComponentPool* p = poolFor(self->components[i].typeId);
    u32 slot = self->components[i].slot;
    if (!p || !p->type->asBehaviour) continue;
    if ((p->flags[slot] & (kSlotAlive | kSlotDying | kSlotStarted)) != (kSlotAlive | kSlotStarted)) continue;
    Behaviour* b = p->type->asBehaviour(p->at(slot));
    switch (kind) {
      case Contact::TriggerEnter: b->OnTriggerEnter(other); break;
      case Contact::TriggerExit: b->OnTriggerExit(other); break;
      case Contact::TriggerEnter2D: b->OnTriggerEnter2D(other); break;
      case Contact::TriggerExit2D: b->OnTriggerExit2D(other); break;
      case Contact::CollisionEnter2D: b->OnCollisionEnter2D(other); break;
      case Contact::CollisionExit2D: b->OnCollisionExit2D(other); break;
    }
  }
}

void World::sendUIClick(GameObject* self) {
  if (!self || self->dying) return;
  for (u32 i = 0; i < self->componentCount; ++i) {
    ComponentPool* p = poolFor(self->components[i].typeId);
    u32 slot = self->components[i].slot;
    if (!p || !p->type->asBehaviour) continue;
    if ((p->flags[slot] & (kSlotAlive | kSlotDying | kSlotStarted)) != (kSlotAlive | kSlotStarted)) continue;
    p->type->asBehaviour(p->at(slot))->OnUIClick();
  }
}

void World::destroyAllImmediate(bool keepDontDestroy) {
  for (u32 i = 0; i < objectHighWater_; ++i) {
    GameObject* go = &objects_[i];
    if (go->alive && !go->dying && !go->transform.parent_ && !(keepDontDestroy && go->dontDestroyOnLoad)) destroy(go);
  }
  flushNow();
}

void World::dontDestroyOnLoad(GameObject* go) {
  if (!go || !go->alive || go->dying) return;
  if (go->transform.parent_) go->transform.SetParent(nullptr, true);
  go->dontDestroyOnLoad = true;
}

// ---- Instantiate / delayed Destroy -----------------------------------------

GameObject* World::cloneSubtree(const GameObject* src, Transform* parent, bool root) {
  u32 childTotal = src->transform.childCount();  // before the clone exists: it may be attached below `src`
  GameObject* go = createObject(src->name, parent ? parent->gameObject : nullptr);
  if (!go) return nullptr;
  if (root) {
    size_t n = strlen(go->name);
    static const char suffix[] = " (Clone)";
    if (n + sizeof(suffix) <= kMaxNameLength) memcpy(go->name + n, suffix, sizeof(suffix));
  }
  memcpy(go->tag, src->tag, sizeof(go->tag));
  go->layer = src->layer;
  go->selfActive_ = src->selfActive_;
  go->transform.lpos_ = src->transform.lpos_;
  go->transform.lrot_ = src->transform.lrot_;
  go->transform.lscale_ = src->transform.lscale_;
  for (u32 i = 0; i < src->componentCount; ++i) {
    const ComponentPool* sp = poolFor(src->components[i].typeId);
    u32 sslot = src->components[i].slot;
    if (!sp || (sp->flags[sslot] & (kSlotAlive | kSlotDying)) != kSlotAlive) continue;
    const TypeInfo& type = *sp->type;
    Component* c = addComponent(go, type);
    if (!c) {
      destroy(go);
      return nullptr;
    }
    u8 blob[kMaxFields * 16];
    u32 size = blobSize(type);
    const void* smem = sp->at(sslot);
    if (size <= sizeof(blob)) {
      writeBlob(type, smem, blob);
      readBlob(type, c, blob, size);
    }
    const Component* sc = type.asComponent(const_cast<void*>(smem));
    c->enabled_ = sc->enabled_;
    c->OnCloned(sc);
  }
  for (u32 i = 0; i < childTotal; ++i) {
    const Transform* child = src->transform.GetChild(i);
    if (!child || child->gameObject->dying) continue;
    if (!cloneSubtree(child->gameObject, &go->transform, false)) {
      destroy(go);
      return nullptr;
    }
  }
  return go;
}

GameObject* World::instantiate(const GameObject* original, Transform* parent, const Vector3* pos, const Quaternion* rot,
                               bool keepLocalUnderParent) {
  if (!original || !original->alive || original->dying) return nullptr;
  ++cloneDepth_;
  GameObject* root = cloneSubtree(original, parent, true);
  --cloneDepth_;
  if (!root) {
    RLOG_ERROR("world", "Instantiate('%s') failed: object or component capacity exhausted", original->name);
    return nullptr;
  }
  if (!parent && original->transform.parent_) {
    // A clone of a child that gets no parent becomes a root and keeps the original's world placement.
    root->transform.lpos_ = original->transform.position();
    root->transform.lrot_ = original->transform.rotation();
    root->transform.lscale_ = original->transform.lossyScale();
  }
  if (pos) root->transform.setPosition(*pos);
  else if (parent && !keepLocalUnderParent) root->transform.setPosition(original->transform.position());
  if (rot) root->transform.setRotation(*rot);
  else if (parent && !keepLocalUnderParent) root->transform.setRotation(original->transform.rotation());
  awakeSubtree(root);
  return root;
}

bool World::addDelayed(const DelayedDestroy& d) {
  for (u32 i = 0; i < delayedCount_; ++i) {
    if (delayed_[i].typeId == d.typeId && delayed_[i].index == d.index && delayed_[i].generation == d.generation) {
      if (d.remaining < delayed_[i].remaining) delayed_[i].remaining = d.remaining;
      return true;
    }
  }
  if (delayedCount_ >= config_.maxDelayedDestroys) return false;
  delayed_[delayedCount_++] = d;
  return true;
}

void World::destroy(GameObject* go, Scalar delay) {
  if (!go || !go->alive || go->dying) return;
  if (delay <= 0) {
    destroy(go);
    return;
  }
  DelayedDestroy d;
  d.index = go->index;
  d.generation = go->generation;
  d.remaining = delay;
  if (!addDelayed(d)) {
    RLOG_WARN("world", "delayed-destroy table full (%u): destroying '%s' at the end of this frame", config_.maxDelayedDestroys,
              go->name);
    destroy(go);
  }
}

void World::destroy(Component* c, Scalar delay) {
  if (!c) return;
  if (delay <= 0) {
    destroy(c);
    return;
  }
  ComponentPool* p = poolFor(c->typeId_);
  if (!p || (p->flags[c->slot_] & kSlotDying)) return;
  DelayedDestroy d;
  d.typeId = c->typeId_;
  d.index = c->slot_;
  d.generation = p->generations[c->slot_];
  d.remaining = delay;
  if (!addDelayed(d)) {
    RLOG_WARN("world", "delayed-destroy table full (%u): destroying a '%s' at the end of this frame", config_.maxDelayedDestroys,
              p->type->name);
    destroy(c);
  }
}

void World::tickDelayed(Scalar dt) {
  u32 w = 0;
  for (u32 i = 0; i < delayedCount_; ++i) {
    DelayedDestroy d = delayed_[i];
    bool valid;
    if (d.typeId == kInvalidIndex) {
      valid = d.index < objectHighWater_ && objects_[d.index].alive && !objects_[d.index].dying &&
              objects_[d.index].generation == d.generation;
    } else {
      const ComponentPool* p = poolFor(d.typeId);
      valid = p && d.index < p->highWater && (p->flags[d.index] & (kSlotAlive | kSlotDying)) == kSlotAlive &&
              p->generations[d.index] == d.generation;
    }
    if (!valid) continue;  // already destroyed some other way
    d.remaining -= dt;
    if (d.remaining <= 0) {
      if (d.typeId == kInvalidIndex) destroy(&objects_[d.index]);
      else destroy(poolFor(d.typeId)->type->asComponent(poolFor(d.typeId)->at(d.index)));
      continue;
    }
    delayed_[w++] = d;
  }
  delayedCount_ = w;
}

void World::destroyImmediate(GameObject* go) {
  destroy(go);
  flushNow();
}

}  // namespace rensga

// ---- handles (F3.05) -------------------------------------------------------------------------------------------------

namespace rensga {

bool World::handleOf(const Component* c, ComponentHandle& out) const {
  if (!c || c->world_ != this) return false;
  const ComponentPool* p = poolFor(c->typeId_);
  if (!p || c->slot_ >= p->highWater) return false;
  out.world = const_cast<World*>(this);
  out.typeId = c->typeId_;
  out.slot = c->slot_;
  out.generation = p->generations[c->slot_];
  return true;
}

Component* World::resolveHandle(const ComponentHandle& h) const {
  if (h.world != this || h.typeId == kInvalidIndex) return nullptr;
  const ComponentPool* p = poolFor(h.typeId);
  if (!p || h.slot >= p->highWater) return nullptr;
  if ((p->flags[h.slot] & (kSlotAlive | kSlotDying)) != kSlotAlive || p->generations[h.slot] != h.generation) return nullptr;
  return static_cast<Component*>(p->type->asComponent(p->at(h.slot)));
}

Component* ComponentHandle::resolve() const { return world ? world->resolveHandle(*this) : nullptr; }

}  // namespace rensga
