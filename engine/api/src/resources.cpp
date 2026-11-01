// F6 (A3, API 9): Resources::Load<T> through the RSRC section. Assets under Assets/Resources/ are in the pack whether a scene uses them
// or not (the cooker packs every asset), RSRC maps "path/without/extension" to their GUID and kind. A handle is {guid, kind, slot};
// the slot table counts references. Texture, audio, material, mesh, font and similar kinds live in the pack for the whole run, so
// Unload only drops the count and frees the slot; a data asset (kind DataAsset) gets a typed instance in the slot, built from its
// DATA blob with readBlob and destroyed on the last Unload. No heap: 32 slots with 256 bytes of instance storage each.
#include <string.h>

#include "rensga/Resources.h"
#include "rensga/core/log.h"
#include "rensga/core/reflect.h"
#include "rensga/core/scene_manager.h"

namespace rensga {

namespace {
constexpr u32 kMaxSlots = 32;
constexpr u32 kDataBytes = 256;
struct Slot {
  Guid guid;
  PackResourceKind kind = PackResourceKind::Texture;
  u32 refs = 0;
  const TypeInfo* type = nullptr;  // data assets
  alignas(16) u8 data[kDataBytes];
};
Slot g_slots[kMaxSlots];

bool sameText(const char* a, const char* b) {  // paths are matched case-insensitively, '\\' = '/'
  for (; *a && *b; ++a, ++b) {
    char x = *a == '\\' ? '/' : *a, y = *b == '\\' ? '/' : *b;
    if (x >= 'A' && x <= 'Z') x = static_cast<char>(x - 'A' + 'a');
    if (y >= 'A' && y <= 'Z') y = static_cast<char>(y - 'A' + 'a');
    if (x != y) return false;
  }
  return *a == *b;
}

const char* kindName(PackResourceKind k) {
  switch (k) {
    case PackResourceKind::Texture: return "texture";
    case PackResourceKind::Audio: return "audio clip";
    case PackResourceKind::Material: return "material";
    case PackResourceKind::Mesh: return "mesh";
    case PackResourceKind::Font: return "font";
    case PackResourceKind::DataAsset: return "data asset";
    case PackResourceKind::Prefab: return "prefab";
    case PackResourceKind::AnimatorController: return "animator controller";
    case PackResourceKind::PropClip: return "property clip";
    case PackResourceKind::Scene: return "scene";
  }
  return "?";
}
}  // namespace

const void* ResourceHandle::dataPointer() const {
  if (slot >= kMaxSlots || kind != PackResourceKind::DataAsset) return nullptr;
  const Slot& s = g_slots[slot];
  return (s.refs && s.guid == guid && s.type) ? s.data : nullptr;
}

ResourceHandle Resources::LoadRaw(const char* path, PackResourceKind kind) {
  ResourceHandle none;
  const PackView* pack = SceneManager::pack();
  if (!pack || !path || !*path) return none;
  const PackContent c = pack->content();
  const PackResource* r = c.findResource(packResourceNameHash(path));
  const PackResource* found = nullptr;
  u32 wrongKind = 0;
  for (; r && r < c.resources + c.resourceCount && r->nameHash == packResourceNameHash(path); ++r) {
    if (!sameText(pack->string(r->nameStr), path)) continue;
    if (r->kind != static_cast<u32>(kind)) { wrongKind = r->kind; continue; }
    found = r;
    break;
  }
  if (!found) {
    if (wrongKind) RLOG_WARN("resources", "Resources.Load(\"%s\"): it is a %s, not a %s", path, kindName(static_cast<PackResourceKind>(wrongKind)), kindName(kind));
    else RLOG_WARN("resources", "Resources.Load(\"%s\"): no such asset under a Resources folder", path);
    return none;
  }
  Slot* free = nullptr;
  u32 freeIndex = kInvalidIndex;
  for (u32 i = 0; i < kMaxSlots; ++i) {
    if (g_slots[i].refs && g_slots[i].guid == found->guid) {
      ++g_slots[i].refs;
      ResourceHandle h;
      h.guid = found->guid;
      h.kind = kind;
      h.slot = i;
      return h;
    }
    if (!g_slots[i].refs && !free) { free = &g_slots[i]; freeIndex = i; }
  }
  if (!free) {
    RLOG_ERROR("resources", "Resources.Load(\"%s\"): all %u handles are in use (Unload what you no longer need)", path, kMaxSlots);
    return none;
  }
  free->guid = found->guid;
  free->kind = kind;
  free->type = nullptr;
  if (kind == PackResourceKind::DataAsset) {
    const PackDataAsset* d = c.findData(found->guid);
    const TypeInfo* t = d ? TypeRegistry::instance().findByHash(d->typeHash) : nullptr;
    if (!d || !t || t->size > kDataBytes || t->align > 16 || !t->construct) {
      RLOG_ERROR("resources", "Resources.Load(\"%s\"): the data asset is missing, its type is unknown or larger than %u bytes", path, kDataBytes);
      return none;
    }
    t->construct(free->data);
    if (!readBlob(*t, free->data, pack->section(pack_section::BLOB) + d->blobOffset, d->blobSize)) {
      RLOG_ERROR("resources", "Resources.Load(\"%s\"): the data blob does not match type '%s' (%u bytes)", path, t->name, d->blobSize);
      if (t->destruct) t->destruct(free->data);
      return none;
    }
    free->type = t;
  }
  free->refs = 1;
  ResourceHandle h;
  h.guid = found->guid;
  h.kind = kind;
  h.slot = freeIndex;
  return h;
}

void Resources::Unload(ResourceHandle& handle) {
  if (handle.slot < kMaxSlots) {
    Slot& s = g_slots[handle.slot];
    if (s.refs && s.guid == handle.guid) {
      if (--s.refs == 0 && s.type) {
        if (s.type->destruct) s.type->destruct(s.data);
        s.type = nullptr;
      }
    }
  }
  handle = ResourceHandle();
}

u32 Resources::loadedCount() {
  u32 n = 0;
  for (u32 i = 0; i < kMaxSlots; ++i)
    if (g_slots[i].refs) ++n;
  return n;
}

u32 Resources::entryCount() {
  const PackView* pack = SceneManager::pack();
  return pack ? pack->content().resourceCount : 0;
}

}  // namespace rensga
