#include <string.h>

#include "rensga/core/components.h"
#include "rensga/core/log.h"
#include "rensga/core/world.h"

namespace rensga {

bool PackView::open(const u8* data, size_t byteSize) {
  *this = PackView();
  if (!data || byteSize < sizeof(PackHeader)) {
    RLOG_ERROR("pack", "pack too small (%zu bytes)", byteSize);
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(data) & 15) != 0) {
    RLOG_ERROR("pack", "pack memory is not 16-byte aligned");
    return false;
  }
  const PackHeader* h = reinterpret_cast<const PackHeader*>(data);
  if (memcmp(h->magic, "RPAK", 4) != 0) {
    RLOG_ERROR("pack", "bad magic, not a Rensga pack");
    return false;
  }
  if (h->version < kPackMinVersion || h->version > kPackVersion) {
    RLOG_ERROR("pack", "unsupported pack version %u (this runtime reads versions %u to %u)", h->version, kPackMinVersion,
               kPackVersion);
    return false;
  }
  if (h->totalSize > byteSize || sizeof(PackHeader) + static_cast<size_t>(h->sectionCount) * sizeof(PackSection) > h->totalSize) {
    RLOG_ERROR("pack", "pack truncated: header says %u bytes, have %zu", h->totalSize, byteSize);
    return false;
  }
  const PackSection* secs = reinterpret_cast<const PackSection*>(data + sizeof(PackHeader));
  for (u32 i = 0; i < h->sectionCount; ++i) {
    if (secs[i].offset > h->totalSize || secs[i].size > h->totalSize - secs[i].offset || (secs[i].offset & 15)) {
      RLOG_ERROR("pack", "section %u out of bounds or misaligned", i);
      return false;
    }
  }
  base = data;
  size = h->totalSize;
  header = h;
  sections = secs;
  return true;
}

const PackSection* PackView::find(u32 fourcc) const {
  for (u32 i = 0; header && i < header->sectionCount; ++i)
    if (sections[i].fourcc == fourcc) return &sections[i];
  return nullptr;
}

const char* PackView::string(u32 byteOffset) const {
  const PackSection* s = find(pack_section::STRS);
  if (!s || byteOffset >= s->size) return "";
  return reinterpret_cast<const char*>(base + s->offset + byteOffset);
}

const char* PackView::stringByIndex(u32 index) const {
  const PackSection* s = find(pack_section::STRS);
  if (!s || index >= s->count) return "";
  const char* p = reinterpret_cast<const char*>(base + s->offset);
  const char* end = p + s->size;
  for (u32 i = 0; i < index && p < end; ++i) p += strlen(p) + 1;
  return p < end ? p : "";
}

bool World::loadPack(const PackView& pack, LoadedPack* out) {
  u32 objCount = 0, xfrmCount = 0, compCount = 0, meshCount = 0, vertCount = 0, inputCount = 0;
  const PackObject* objs = pack.records<PackObject>(pack_section::OBJS, &objCount);
  const PackTransform* xfrms = pack.records<PackTransform>(pack_section::XFRM, &xfrmCount);
  const PackComponent* comps = pack.records<PackComponent>(pack_section::COMP, &compCount);
  const u8* fdat = pack.section(pack_section::FDAT);
  const PackSection* fdatSec = pack.find(pack_section::FDAT);
  const PackMeshRec* meshes = pack.records<PackMeshRec>(pack_section::MESH, &meshCount);
  const PackVertex* verts = pack.records<PackVertex>(pack_section::VERT, &vertCount);
  const PackInputAction* inputs = pack.records<PackInputAction>(pack_section::INPT, &inputCount);
  const PackConsoleTarget* ctgt = pack.records<PackConsoleTarget>(pack_section::CTGT);

  if (!objs || xfrmCount != objCount) {
    RLOG_ERROR("pack", "scene sections missing or inconsistent (OBJS=%u XFRM=%u)", objCount, xfrmCount);
    return false;
  }
  if (objCount > config_.maxObjects) {
    RLOG_ERROR("pack", "scene has %u objects, capacity is %u", objCount, config_.maxObjects);
    return false;
  }

  // Count components per type to size each pool from scene data.
  u32 perType[kMaxTypes] = {};
  for (u32 i = 0; i < compCount; ++i) {
    const TypeInfo* t = TypeRegistry::instance().findByHash(comps[i].typeHash);
    if (!t) {
      RLOG_ERROR("pack", "unknown component type hash 0x%08x (script not linked into this build?)", comps[i].typeHash);
      return false;
    }
    if (!fdatSec || comps[i].fieldOffset > fdatSec->size || comps[i].fieldSize > fdatSec->size - comps[i].fieldOffset ||
        comps[i].objectIndex >= objCount) {
      RLOG_ERROR("pack", "component %u of type '%s' has bad offsets", i, t->name);
      return false;
    }
    ++perType[t->id];
  }
  for (u32 id = 0; id < kMaxTypes; ++id) {
    if (!perType[id]) continue;
    const TypeInfo* t = TypeRegistry::instance().byId(id);
    u32 cap = perType[id] + config_.spareComponents;
    if (cap > config_.maxComponentsPerType) cap = config_.maxComponentsPerType;
    if (!reservePool(*t, cap < perType[id] ? perType[id] : cap)) return false;
  }

  // Objects first (parents may come later in the table), then parent links.
  size_t mark = arena_->used();
  u32* map = arena_->allocateArray<u32>(objCount ? objCount : 1);
  if (!map) return false;
  for (u32 i = 0; i < objCount; ++i) {
    GameObject* go = createObject(pack.string(objs[i].nameStr));
    if (!go) {
      arena_->rewind(mark);
      return false;
    }
    go->guid = objs[i].guid;
    go->selfActive_ = objs[i].active != 0;
    go->layer = objs[i].layer;
    if (pack.header->version >= 2 && objs[i].tag != 0) {
      const char* tag = pack.stringByIndex(objs[i].tag);
      if (*tag) go->SetTag(tag);
    }
    go->transform.lpos_ = Vector3(xfrms[i].position[0], xfrms[i].position[1], xfrms[i].position[2]);
    go->transform.lrot_ = Quaternion(xfrms[i].rotation[0], xfrms[i].rotation[1], xfrms[i].rotation[2], xfrms[i].rotation[3]);
    go->transform.lscale_ = Vector3(xfrms[i].scale[0], xfrms[i].scale[1], xfrms[i].scale[2]);
    map[i] = go->index;
  }
  for (u32 i = 0; i < objCount; ++i) {
    i32 p = objs[i].parentIndex;
    if (p >= 0 && static_cast<u32>(p) < objCount) objects_[map[i]].transform.setParent(&objects_[map[p]].transform);
  }

  bool ok = true;
  for (u32 i = 0; i < compCount && ok; ++i) {
    const TypeInfo* t = TypeRegistry::instance().findByHash(comps[i].typeHash);
    GameObject* go = &objects_[map[comps[i].objectIndex]];
    Component* c = addComponent(go, *t);
    if (!c) {
      ok = false;
      break;
    }
    if (!readBlob(*t, c, fdat + comps[i].fieldOffset, comps[i].fieldSize)) {
      RLOG_ERROR("pack", "field blob of '%s' on '%s' is %u bytes, expected %u (cook/runtime mismatch)", t->name,
                 go->name, comps[i].fieldSize, blobSize(*t));
      ok = false;
    }
  }

  for (u32 i = 0; i < meshCount && ok; ++i) {
    if (meshes[i].objectIndex >= objCount) continue;
    GameObject* go = &objects_[map[meshes[i].objectIndex]];
    if (MeshRenderer* mr = getComponent<MeshRenderer>(go)) mr->meshIndex = i;
  }
  arena_->rewind(mark);
  if (!ok) return false;

  if (out) {
    out->meshes.meshes = meshes;
    out->meshes.meshCount = meshCount;
    out->meshes.vertices = verts;
    out->meshes.vertexCount = vertCount;
    out->inputs = inputs;
    out->inputCount = inputCount;
    out->hasTarget = ctgt != nullptr;
    if (ctgt) out->target = *ctgt;
    out->objectCount = objCount;
    out->componentCount = compCount;
  }
  return true;
}

}  // namespace rensga
