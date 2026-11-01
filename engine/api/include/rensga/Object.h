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

// Prefab forms: `prefab` is the Guid field a script exposes (the cooker stores each prefab as an inactive template object).
// Looks the template up in the active World, clones it and activates the clone, like Unity's Instantiate(prefab, pos, rot).
// Returns null for a null/unknown Guid or when a pool is full.
inline GameObject* Instantiate(const Guid& prefab, const Vector3& position, const Quaternion& rotation, Transform* parent = nullptr) {
  World* w = World::active();
  const GameObject* templ = w && !prefab.isNull() ? w->findByGuid(prefab) : nullptr;
  GameObject* go = templ ? Instantiate(templ, position, rotation, parent) : nullptr;
  if (go) go->SetActive(true);
  return go;
}
inline GameObject* Instantiate(const Guid& prefab) {
  World* w = World::active();
  const GameObject* templ = w && !prefab.isNull() ? w->findByGuid(prefab) : nullptr;
  GameObject* go = templ ? Instantiate(templ) : nullptr;
  if (go) go->SetActive(true);
  return go;
}

// The object (moved to the scene root first) and its children survive SceneManager::LoadScene.
inline void DontDestroyOnLoad(GameObject* go) {
  if (go && go->world) go->world->dontDestroyOnLoad(go);
}

inline void Destroy(GameObject* go, Scalar delay = 0) {
  if (go) go->world->destroy(go, delay);
}
inline void Destroy(Component* c, Scalar delay = 0) {
  if (c && c->gameObject()) c->gameObject()->world->destroy(c, delay);
}

}  // namespace rensga
