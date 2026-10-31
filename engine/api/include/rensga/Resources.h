// F6 contract (C0): Unity's Resources.Load / Unload. Assets under a project "Resources" folder are cooked into the pack with their
// path (RSRC: name hash -> GUID + kind) even when no scene references them. Load returns a small handle (no heap); the data stays in
// the pack until the last handle is Unloaded (A3 counts references; unreferenced assets are only released for kinds that live
// outside the pack's BLOB). Stub until A3 (Stage 1, API 9): every Load returns an invalid handle. Defined in engine/api/src/scene_api.cpp.
//
//   Handle<ResAudioClip> h = Resources::Load<ResAudioClip>("sfx/jump");   // path relative to a Resources folder, no extension
//   if (h.valid()) AudioSource::PlayClipAtPoint(h.guid, pos);
//   Handle<EnemyStats> stats = Resources::Load<EnemyStats>("data/orc");     // a data asset of a reflected type: stats.get()->hp
#pragma once
#include "rensga/core/guid.h"
#include "rensga/core/pack_format.h"

namespace rensga {

// Asset kind tags for Resources::Load<T>. Any other T is a data asset (DATA section) of that reflected type.
struct ResTexture {};
struct ResAudioClip {};
struct ResMaterial {};
struct ResMesh {};
struct ResFont {};
struct ResPrefab {};  // Instantiate-able object tree (the cooker merges prefabs; A1/A3 decide the runtime form)
struct ResAnimatorController {};
struct ResPropertyClip {};

template <class T> struct ResourceKindOf { static constexpr PackResourceKind value = PackResourceKind::DataAsset; };
template <> struct ResourceKindOf<ResTexture> { static constexpr PackResourceKind value = PackResourceKind::Texture; };
template <> struct ResourceKindOf<ResAudioClip> { static constexpr PackResourceKind value = PackResourceKind::Audio; };
template <> struct ResourceKindOf<ResMaterial> { static constexpr PackResourceKind value = PackResourceKind::Material; };
template <> struct ResourceKindOf<ResMesh> { static constexpr PackResourceKind value = PackResourceKind::Mesh; };
template <> struct ResourceKindOf<ResFont> { static constexpr PackResourceKind value = PackResourceKind::Font; };
template <> struct ResourceKindOf<ResPrefab> { static constexpr PackResourceKind value = PackResourceKind::Prefab; };
template <> struct ResourceKindOf<ResAnimatorController> { static constexpr PackResourceKind value = PackResourceKind::AnimatorController; };
template <> struct ResourceKindOf<ResPropertyClip> { static constexpr PackResourceKind value = PackResourceKind::PropClip; };

// Untyped handle: the asset GUID, its kind and a reference-count slot. Copy it freely; call Resources::Unload once per Load.
struct ResourceHandle {
  Guid guid;
  PackResourceKind kind = PackResourceKind::Texture;
  u32 slot = kInvalidIndex;
  bool valid() const { return slot != kInvalidIndex && !guid.isNull(); }
  // The DATA blob of a data asset (null for other kinds); read it with readBlob(*TypeRegistry::find...). Typed access: Handle<T>.
  const void* dataPointer() const;
};

template <class T>
struct Handle : ResourceHandle {
  Handle() {}
  explicit Handle(const ResourceHandle& h) : ResourceHandle(h) {}
  // Data assets only: a pointer to the loaded instance of T (read only), null if the handle is invalid or not a data asset.
  const T* get() const { return static_cast<const T*>(dataPointer()); }
  const T* operator->() const { return get(); }
};

namespace Resources {
// Untyped: path relative to a Resources folder without extension, matched case-insensitively.
ResourceHandle LoadRaw(const char* path, PackResourceKind kind);
void Unload(ResourceHandle& handle);  // drops one reference and invalidates the handle
// Number of assets currently loaded (references > 0) and the RSRC entries in the pack.
u32 loadedCount();
u32 entryCount();

template <class T>
Handle<T> Load(const char* path) {
  return Handle<T>(LoadRaw(path, ResourceKindOf<T>::value));
}
}  // namespace Resources

}  // namespace rensga
