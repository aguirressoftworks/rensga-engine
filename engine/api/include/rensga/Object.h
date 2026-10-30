// API 2: Unity's Object-level functions. Instantiate clones an object subtree by copying reflected fields,
// Destroy is deferred to the end of the frame (Refs read null as soon as the delay has passed).
// DestroyImmediate is deliberately not part of the script API.
#pragma once
#include "rensga/core/application.h"
#include "rensga/core/world.h"

namespace rensga {

// Clone at the original's place. Returns null if an object or component pool is full.
inline GameObject* Instantiate(const GameObject* original) {
  return original ? original->world->instantiate(original) : nullptr;
}
// Clone at a world-space position and rotation, optionally under `parent`.
inline GameObject* Instantiate(const GameObject* original, const Vector3& position, const Quaternion& rotation,
                               Transform* parent = nullptr) {
  return original ? original->world->instantiate(original, parent, &position, &rotation) : nullptr;
}
// Clone under `parent`. With instantiateInWorldSpace the clone keeps the original's world placement,
// otherwise it keeps the original's local values (Unity's default).
inline GameObject* Instantiate(const GameObject* original, Transform* parent, bool instantiateInWorldSpace = false) {
  return original ? original->world->instantiate(original, parent, nullptr, nullptr, !instantiateInWorldSpace) : nullptr;
}
// Component forms clone the owning object and return the clone's component of the same type.
template <class T>
T* Instantiate(const T* original) {
  if (!original) return nullptr;
  GameObject* go = Instantiate(original->gameObject());
  return go ? go->template GetComponent<T>() : nullptr;
}
template <class T>
T* Instantiate(const T* original, const Vector3& position, const Quaternion& rotation, Transform* parent = nullptr) {
  if (!original) return nullptr;
  GameObject* go = Instantiate(original->gameObject(), position, rotation, parent);
  return go ? go->template GetComponent<T>() : nullptr;
}

inline void Destroy(GameObject* go, Scalar delay = 0) {
  if (go) go->world->destroy(go, delay);
}
inline void Destroy(Component* c, Scalar delay = 0) {
  if (c && c->gameObject()) c->gameObject()->world->destroy(c, delay);
}

}  // namespace rensga
