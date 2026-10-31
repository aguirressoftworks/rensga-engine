// F6 contract (C0): fixed-capacity pools, no heap. A3 (Stage 1, API 12) adds profiler samples around Get/Release and the budget
// hooks; the behaviour below is final.
//
//   ObjectPool<Bullet, 32> bullets;                 // 32 Bullets live inside the pool object itself
//   Bullet* b = bullets.Get();  ...  bullets.Release(b);
//
//   GameObjectPool<16> pool;  pool.Init(prefab);    // 16 inactive clones made once (at Awake / Start)
//   GameObject* shot = pool.Get(position, rotation);  ...  pool.Release(shot);   // SetActive(true) / SetActive(false)
#pragma once
#include "rensga/core/object.h"
#include "rensga/core/world.h"
#include "rensga/Object.h"

namespace rensga {

template <class T, u32 N>
class ObjectPool {
  static_assert(N > 0, "empty pool");

 public:
  ObjectPool() { Reset(); }
  // A default-constructed T (reset on every Get), or null when the pool is exhausted.
  T* Get() {
    if (freeCount_ == 0) return nullptr;
    u32 i = free_[--freeCount_];
    used_[i] = true;
    items_[i] = T();
    return &items_[i];
  }
  // False when `item` is not from this pool or was already released.
  bool Release(T* item) {
    if (item < items_ || item >= items_ + N) return false;
    u32 i = static_cast<u32>(item - items_);
    if (!used_[i]) return false;
    used_[i] = false;
    free_[freeCount_++] = i;
    return true;
  }
  void Reset() {
    for (u32 i = 0; i < N; ++i) {
      used_[i] = false;
      free_[i] = N - 1 - i;  // Get hands out slot 0 first
    }
    freeCount_ = N;
  }
  static constexpr u32 capacity() { return N; }
  u32 activeCount() const { return N - freeCount_; }
  u32 availableCount() const { return freeCount_; }
  template <class F>
  void ForEachActive(F f) {
    for (u32 i = 0; i < N; ++i)
      if (used_[i]) f(items_[i]);
  }

 private:
  T items_[N];
  bool used_[N];
  u32 free_[N];
  u32 freeCount_ = 0;
};

template <u32 N>
class GameObjectPool {
  static_assert(N > 0, "empty pool");

 public:
  // Makes N inactive clones of `prefab` (a scene object used as a template; Instantiate does the copy). Returns how many fit
  // (less than N when the world's object pool is full). Call once.
  u32 Init(const GameObject* prefab) {
    count_ = 0;
    for (u32 i = 0; i < N && prefab; ++i) {
      GameObject* go = Instantiate(prefab);
      if (!go) break;
      go->SetActive(false);
      items_[count_++] = go->ref();
    }
    return count_;
  }
  // An inactive clone, activated at the given pose; null when every clone is in use.
  GameObject* Get(const Vector3& position = Vector3(), const Quaternion& rotation = Quaternion()) {
    for (u32 i = 0; i < count_; ++i) {
      GameObject* go = items_[i].get();
      if (go && !go->activeSelf()) {
        go->transform.setPosition(position);
        go->transform.setRotation(rotation);
        go->SetActive(true);
        return go;
      }
    }
    return nullptr;
  }
  void Release(GameObject* go) {
    if (go) go->SetActive(false);
  }
  u32 capacity() const { return count_; }
  u32 availableCount() const {
    u32 n = 0;
    for (u32 i = 0; i < count_; ++i) {
      const GameObject* go = items_[i].get();
      if (go && !go->activeSelf()) ++n;
    }
    return n;
  }

 private:
  Ref<GameObject> items_[N];
  u32 count_ = 0;
};

}  // namespace rensga
