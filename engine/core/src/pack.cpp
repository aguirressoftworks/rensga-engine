#include <string.h>

#include "rensga/core/components.h"
#include "rensga/core/log.h"
#include "rensga/core/unity_event.h"
#include "rensga/core/world.h"

namespace rensga {

static bool validateContent(const PackView& pk, const char* nm);  // F6 content sections, below

bool PackView::open(const u8* data, size_t byteSize, const char* name) {
  *this = PackView();
  const char* nm = name ? name : "<memory>";
  if (!data || byteSize < sizeof(PackHeader)) {
    RLOG_ERROR("pack", "%s: pack too small (%zu bytes)", nm, byteSize);
    return false;
  }
  if ((reinterpret_cast<uintptr_t>(data) & 15) != 0) {
    RLOG_ERROR("pack", "%s: pack memory is not 16-byte aligned", nm);
    return false;
  }
  const PackHeader* h = reinterpret_cast<const PackHeader*>(data);
  if (memcmp(h->magic, "RPAK", 4) != 0) {
    RLOG_ERROR("pack", "%s: bad magic, not a Rensga pack", nm);
    return false;
  }
  if (h->version < kPackMinVersion || h->version > kPackVersion) {
    RLOG_ERROR("pack", "%s: unsupported pack version %u (this runtime reads versions %u to %u)", nm, h->version, kPackMinVersion,
               kPackVersion);
    return false;
  }
  if (h->totalSize > byteSize || sizeof(PackHeader) + static_cast<size_t>(h->sectionCount) * sizeof(PackSection) > h->totalSize) {
    RLOG_ERROR("pack", "%s: pack truncated: header says %u bytes, have %zu", nm, h->totalSize, byteSize);
    return false;
  }
  const PackSection* secs = reinterpret_cast<const PackSection*>(data + sizeof(PackHeader));
  for (u32 i = 0; i < h->sectionCount; ++i) {
    if (secs[i].offset > h->totalSize || secs[i].size > h->totalSize - secs[i].offset || (secs[i].offset & 15)) {
      RLOG_ERROR("pack", "%s: section %u out of bounds or misaligned", nm, i);
      return false;
    }
  }
  base = data;
  size = h->totalSize;
  header = h;
  sections = secs;
  if (h->version >= 2 && !validateAssets(nm)) {
    *this = PackView();
    return false;
  }
  return true;
}

// v2: every record's payload range must lie inside BLOB, so readers can index without checks.
bool PackView::validateAssets(const char* nm) const {
  const PackAssets a = assets();
  auto fits = [&](u64 off, u64 bytes) { return off + bytes <= a.blobSize; };
  for (u32 i = 0; i < a.textureCount; ++i) {
    const PackTexRec& t = a.textures[i];
    u64 pal = t.paletteCount * 4ull;
    if (t.format > kPackTexFormatMax || !fits(t.pixelOffset, t.pixelBytes) || (t.paletteCount && !fits(t.paletteOffset, pal))) {
      RLOG_ERROR("pack", "%s: texture %u has a bad format or payload range", nm, i);
      return false;
    }
  }
  for (u32 i = 0; i < a.audioCount; ++i) {
    const PackAudioRec& r = a.audio[i];
    if (r.channels < 1 || r.channels > 2 || !fits(r.dataOffset, r.frameCount * r.channels * 2ull)) {
      RLOG_ERROR("pack", "%s: audio clip %u has a bad payload range", nm, i);
      return false;
    }
  }
  for (u32 i = 0; i < a.tilemapCount; ++i) {
    const PackTilemapRec& m = a.tilemaps[i];
    if (m.tilesetTexture >= a.textureCount || !fits(m.tilesOffset, m.width * m.height * 2ull) || !fits(m.solidOffset, m.solidCount)) {
      RLOG_ERROR("pack", "%s: tilemap %u has a bad tileset or payload range", nm, i);
      return false;
    }
  }
  for (u32 i = 0; i < a.animCount; ++i) {
    const PackAnimRec& n = a.anims[i];
    if (n.texture >= a.textureCount || !fits(n.framesOffset, n.frameCount * sizeof(PackFrameRect))) {
      RLOG_ERROR("pack", "%s: sprite clip %u has a bad texture or payload range", nm, i);
      return false;
    }
  }
  for (u32 i = 0; i < a.fontCount; ++i) {
    if (a.fonts[i].texture >= a.textureCount || a.fonts[i].charCount > kPackFontMaxChars) {
      RLOG_ERROR("pack", "%s: font %u has a bad texture or glyph count", nm, i);
      return false;
    }
  }
  // F5 shared 3D sections.
  for (u32 i = 0; i < a.skinCount; ++i) {
    const PackSkin& k = a.skins[i];
    if (!fits(k.jointsOffset, k.jointCount * sizeof(PackJoint)) || (k.jointsOffset & 15)) {
      RLOG_ERROR("pack", "%s: skin %u has a bad joint range", nm, i);
      return false;
    }
    const PackJoint* j = a.skinJoints(k);
    for (u32 q = 0; q < k.jointCount; ++q)
      if (j[q].parent >= static_cast<i32>(q) || j[q].parent < -1) {
        RLOG_ERROR("pack", "%s: skin %u joint %u has a parent that does not come before it", nm, i, q);
        return false;
      }
  }
  for (u32 i = 0; i < a.clip3Count; ++i) {
    const PackClip3& c = a.clips3[i];
    if (c.skin >= a.skinCount || c.fps == 0 || !fits(c.dataOffset, static_cast<u64>(c.frameCount) * a.skins[c.skin].jointCount * sizeof(PackJointPose)) ||
        (c.dataOffset & 15)) {
      RLOG_ERROR("pack", "%s: skeletal clip %u has a bad skin, fps or pose range", nm, i);
      return false;
    }
  }
  for (u32 i = 0; i < a.materialCount; ++i)
    if (a.materials[i].texture != kInvalidIndex && a.materials[i].texture >= a.textureCount) {
      RLOG_ERROR("pack", "%s: material %u names texture %u of %u", nm, i, a.materials[i].texture, a.textureCount);
      return false;
    }
  u32 meshCount = 0, litCount = 0, batchCount = 0, vtx3Count = 0;
  records<PackMeshRec>(pack_section::MESH, &meshCount);
  const PackMeshLit* lit = records<PackMeshLit>(pack_section::MSHL, &litCount);
  const PackBoneBatch* batches = records<PackBoneBatch>(pack_section::BTCH, &batchCount);
  const PackSection* v3 = find(pack_section::VTX3);
  (void)vtx3Count;
  const u64 v3Size = v3 ? v3->size : 0;
  if (const PackSection* ct = find(pack_section::CTG2); ct && ct->size < sizeof(PackConsoleTargetExt)) {
    RLOG_ERROR("pack", "%s: CTG2 section is too small", nm);
    return false;
  }
  if (!validateContent(*this, nm)) return false;
  if (lit && litCount != meshCount) {
    RLOG_ERROR("pack", "%s: MSHL has %u records but MESH has %u", nm, litCount, meshCount);
    return false;
  }
  for (u32 i = 0; lit && i < litCount; ++i) {
    const PackMeshLit& m = lit[i];
    if (m.isBaked()) continue;
    const u32 stride = packVertexStride(m.vertexFormat);
    if (!stride || (m.vertexByteOffset & 15) || m.vertexCount % 3 != 0 ||
        static_cast<u64>(m.vertexByteOffset) + static_cast<u64>(m.vertexCount) * stride > v3Size) {
      RLOG_ERROR("pack", "%s: mesh %u has a bad vertex format or VTX3 range", nm, i);
      return false;
    }
    if (m.material != kInvalidIndex && m.material >= a.materialCount) {
      RLOG_ERROR("pack", "%s: mesh %u names material %u of %u", nm, i, m.material, a.materialCount);
      return false;
    }
    if (m.isSkinned() && m.skin >= a.skinCount) {
      RLOG_ERROR("pack", "%s: skinned mesh %u has no valid skin", nm, i);
      return false;
    }
    if (m.skin != kInvalidIndex && m.skin >= a.skinCount) {
      RLOG_ERROR("pack", "%s: mesh %u names skin %u of %u", nm, i, m.skin, a.skinCount);
      return false;
    }
    if (m.firstBatch > batchCount || m.batchCount > batchCount - m.firstBatch) {
      RLOG_ERROR("pack", "%s: mesh %u has a bad batch range", nm, i);
      return false;
    }
    for (u32 b = 0; b < m.batchCount; ++b) {
      const PackBoneBatch& bb = batches[m.firstBatch + b];
      if (bb.boneCount > kMaxBatchBones || bb.firstVertex > m.vertexCount || bb.vertexCount > m.vertexCount - bb.firstVertex) {
        RLOG_ERROR("pack", "%s: mesh %u batch %u is out of range", nm, i, b);
        return false;
      }
      for (u32 q = 0; q < bb.boneCount; ++q)
        if (bb.bones[q] != kBatchBoneUnused && m.skin < a.skinCount && bb.bones[q] >= a.skins[m.skin].jointCount) {
          RLOG_ERROR("pack", "%s: mesh %u batch %u names joint %u of %u", nm, i, b, bb.bones[q], a.skins[m.skin].jointCount);
          return false;
        }
    }
  }
  return true;
}

PackContent PackView::content() const {
  PackContent c;
  if (!header) return c;
  const PackSection* b = find(pack_section::BLOB);
  if (b) {
    c.blob = base + b->offset;
    c.blobSize = b->size;
  }
  c.propClips = records<PackPropClip>(pack_section::PCLP, &c.propClipCount);
  c.controllers = records<PackAnimController>(pack_section::ANIC, &c.controllerCount);
  c.data = records<PackDataAsset>(pack_section::DATA, &c.dataCount);
  c.resources = records<PackResource>(pack_section::RSRC, &c.resourceCount);
  c.physics3d = records<PackPhysics3D>(pack_section::PHY3);
  return c;
}

// F6 content sections: every table range must lie inside BLOB (and its section), so readers index without checks.
static bool validateContent(const PackView& pk, const char* nm) {
  const PackContent c = pk.content();
  auto fits = [&](u64 off, u64 count, u64 each) { return count == 0 || (off + count * each <= c.blobSize && (off & 15) == 0); };
  struct Need { u32 fourcc; u32 recordSize; };
  const Need needs[] = {{pack_section::PCLP, sizeof(PackPropClip)}, {pack_section::ANIC, sizeof(PackAnimController)},
                        {pack_section::DATA, sizeof(PackDataAsset)}, {pack_section::RSRC, sizeof(PackResource)},
                        {pack_section::PHY3, sizeof(PackPhysics3D)}};
  for (const Need& n : needs)
    if (const PackSection* s = pk.find(n.fourcc); s && static_cast<u64>(s->count) * n.recordSize > s->size) {
      RLOG_ERROR("pack", "%s: section %.4s is smaller than its record count", nm, reinterpret_cast<const char*>(&n.fourcc));
      return false;
    }
  for (u32 i = 0; i < c.propClipCount; ++i) {
    const PackPropClip& p = c.propClips[i];
    if (p.fps == 0 || !fits(p.curvesOffset, p.curveCount, sizeof(PackPropCurve)) || !fits(p.eventsOffset, p.eventCount, sizeof(PackPropEvent))) {
      RLOG_ERROR("pack", "%s: property clip %u has a bad fps or table range", nm, i);
      return false;
    }
    for (u32 k = 0; k < p.curveCount; ++k)
      if (!fits(c.curves(p)[k].keysOffset, c.curves(p)[k].keyCount, sizeof(PackPropKey))) {
        RLOG_ERROR("pack", "%s: property clip %u curve %u has a bad key range", nm, i, k);
        return false;
      }
  }
  for (u32 i = 0; i < c.controllerCount; ++i) {
    const PackAnimController& a = c.controllers[i];
    if (!fits(a.paramsOffset, a.paramCount, sizeof(PackAnimParam)) || !fits(a.statesOffset, a.stateCount, sizeof(PackAnimState)) ||
        !fits(a.transitionsOffset, a.transitionCount, sizeof(PackAnimTransition)) ||
        !fits(a.conditionsOffset, a.conditionCount, sizeof(PackAnimCondition)) || (a.stateCount && a.defaultState >= a.stateCount) ||
        a.anyFirst > a.transitionCount || a.anyCount > a.transitionCount - a.anyFirst) {
      RLOG_ERROR("pack", "%s: animator controller %u has a bad table range", nm, i);
      return false;
    }
    for (u32 q = 0; q < a.stateCount; ++q) {
      const PackAnimState& st = c.states(a)[q];
      if (st.firstTransition > a.transitionCount || st.transitionCount > a.transitionCount - st.firstTransition ||
          (st.speedParam != kInvalidIndex && st.speedParam >= a.paramCount)) {
        RLOG_ERROR("pack", "%s: animator controller %u state %u has a bad transition range", nm, i, q);
        return false;
      }
    }
    for (u32 q = 0; q < a.transitionCount; ++q) {
      const PackAnimTransition& t = c.transitions(a)[q];
      if (t.toState < 0 || static_cast<u32>(t.toState) >= a.stateCount || t.firstCondition > a.conditionCount ||
          t.conditionCount > a.conditionCount - t.firstCondition) {
        RLOG_ERROR("pack", "%s: animator controller %u transition %u has a bad target or condition range", nm, i, q);
        return false;
      }
    }
    for (u32 q = 0; q < a.conditionCount; ++q)
      if (c.conditions(a)[q].param >= a.paramCount) {
        RLOG_ERROR("pack", "%s: animator controller %u condition %u names a missing parameter", nm, i, q);
        return false;
      }
  }
  for (u32 i = 0; i < c.dataCount; ++i)
    if (static_cast<u64>(c.data[i].blobOffset) + c.data[i].blobSize > c.blobSize) {
      RLOG_ERROR("pack", "%s: data asset %u has a bad blob range", nm, i);
      return false;
    }
  for (u32 i = 1; i < c.resourceCount; ++i)
    if (c.resources[i].nameHash < c.resources[i - 1].nameHash) {
      RLOG_ERROR("pack", "%s: RSRC is not sorted by name hash", nm);
      return false;
    }
  return true;
}

PackAssets PackView::assets() const {
  PackAssets a;
  if (!header) return a;
  const PackSection* b = find(pack_section::BLOB);
  if (b) {
    a.blob = base + b->offset;
    a.blobSize = b->size;
  }
  a.textures = records<PackTexRec>(pack_section::TEXR, &a.textureCount);
  a.audio = records<PackAudioRec>(pack_section::AUDI, &a.audioCount);
  a.tilemaps = records<PackTilemapRec>(pack_section::TMAP, &a.tilemapCount);
  a.anims = records<PackAnimRec>(pack_section::ANIM, &a.animCount);
  a.fonts = records<PackFontRec>(pack_section::FONT, &a.fontCount);
  a.materials = records<PackMaterial>(pack_section::MATL, &a.materialCount);
  a.skins = records<PackSkin>(pack_section::SKIN, &a.skinCount);
  a.clips3 = records<PackClip3>(pack_section::CLP3, &a.clip3Count);
  return a;
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

bool World::loadPack(const PackView& pack, LoadedPack* out, u32 sceneIndex) {
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
  if (const PackSchedulerLimits* lim = pack.records<PackSchedulerLimits>(pack_section::CORO)) {
    if (lim->maxCoroutines && !scheduler_.resize(*arena_, lim->maxCoroutines, lim->maxInvokes))
      RLOG_WARN("pack", "cannot resize the coroutine pool to %u (coroutines are running); keeping the current size", lim->maxCoroutines);
  }

  if (!objs || xfrmCount != objCount) {
    RLOG_ERROR("pack", "scene sections missing or inconsistent (OBJS=%u XFRM=%u)", objCount, xfrmCount);
    return false;
  }
  // Scene range [first, first + count) of OBJS. Without SCNE the pack is one scene.
  u32 sceneCount = 0;
  const PackSceneRec* scenes = pack.records<PackSceneRec>(pack_section::SCNE, &sceneCount);
  u32 first = 0, count = objCount;
  if (scenes) {
    if (sceneIndex >= sceneCount) {
      RLOG_ERROR("pack", "scene index %u is out of range (%u scenes)", sceneIndex, sceneCount);
      return false;
    }
    first = scenes[sceneIndex].firstObject;
    count = scenes[sceneIndex].objectCount;
    if (first > objCount || count > objCount - first) {
      RLOG_ERROR("pack", "scene %u has a bad object range", sceneIndex);
      return false;
    }
  } else if (sceneIndex != 0) {
    RLOG_ERROR("pack", "scene index %u is out of range (1 scene)", sceneIndex);
    return false;
  }
  for (u32 s = 0; s < (scenes ? sceneCount : 1); ++s) {
    const u32 n = scenes ? scenes[s].objectCount : objCount;
    if (n > config_.maxObjects) {
      RLOG_ERROR("pack", "scene has %u objects, capacity is %u", n, config_.maxObjects);
      return false;
    }
  }

  // Count components per type to size each pool from scene data. With several scenes a pool is sized for the
  // largest scene, so any scene can be loaded later into the same pools.
  u32 perType[kMaxTypes] = {};
  u32 perScene[kMaxTypes] = {};
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
    if (comps[i].objectIndex >= first && comps[i].objectIndex - first < count) ++perScene[t->id];
  }
  if (scenes) {
    for (u32 s = 0; s < sceneCount; ++s) {
      u32 c[kMaxTypes] = {};
      for (u32 i = 0; i < compCount; ++i) {
        const TypeInfo* t = TypeRegistry::instance().findByHash(comps[i].typeHash);
        const u32 o = comps[i].objectIndex;
        if (o >= scenes[s].firstObject && o - scenes[s].firstObject < scenes[s].objectCount) ++c[t->id];
      }
      for (u32 id = 0; id < kMaxTypes; ++id)
        if (c[id] > perType[id]) perType[id] = c[id];
    }
  } else {
    for (u32 id = 0; id < kMaxTypes; ++id) perType[id] = perScene[id];
  }
  for (u32 id = 0; id < kMaxTypes; ++id) {
    if (!perType[id]) continue;
    const TypeInfo* t = TypeRegistry::instance().byId(id);
    u32 cap = perType[id] + config_.spareComponents;
    if (cap > config_.maxComponentsPerType) cap = config_.maxComponentsPerType;
    if (!reservePool(*t, cap < perType[id] ? perType[id] : cap)) return false;
  }

  // Objects first (parents may come later in the table), then parent links. `map` is indexed by the scene-local
  // index (global OBJS index minus `first`).
  size_t mark = arena_->used();
  u32* map = arena_->allocateArray<u32>(count ? count : 1);
  if (!map) return false;
  const bool wake = started_;  // a scene loaded into a running world wakes up once complete
  if (wake) ++cloneDepth_;
  for (u32 k = 0; k < count; ++k) {
    const u32 i = first + k;
    GameObject* go = createObject(pack.string(objs[i].nameStr));
    if (!go) {
      if (wake) --cloneDepth_;
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
    map[k] = go->index;
  }
  for (u32 k = 0; k < count; ++k) {
    i32 p = objs[first + k].parentIndex;
    if (p >= static_cast<i32>(first) && static_cast<u32>(p) < first + count)
      objects_[map[k]].transform.setParent(&objects_[map[static_cast<u32>(p) - first]].transform);
  }

  bool ok = true;
  for (u32 i = 0; i < compCount && ok; ++i) {
    if (comps[i].objectIndex < first || comps[i].objectIndex - first >= count) continue;
    const TypeInfo* t = TypeRegistry::instance().findByHash(comps[i].typeHash);
    GameObject* go = &objects_[map[comps[i].objectIndex - first]];
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
    if (meshes[i].objectIndex < first || meshes[i].objectIndex - first >= count) continue;
    GameObject* go = &objects_[map[meshes[i].objectIndex - first]];
    if (MeshRenderer* mr = getComponent<MeshRenderer>(go)) mr->meshIndex = i;
    else if (SkinnedMeshRenderer* sr = getComponent<SkinnedMeshRenderer>(go)) sr->meshIndex = i;
  }
  if (ok) {  // UnityEvent targets are looked up by GUID at fire time; warn now about the ones that cannot resolve
    for (u32 i = 0; i < compCount; ++i) {
      if (comps[i].objectIndex < first || comps[i].objectIndex - first >= count) continue;
      const TypeInfo* t = TypeRegistry::instance().findByHash(comps[i].typeHash);
      GameObject* go = &objects_[map[comps[i].objectIndex - first]];
      Component* c = t ? getComponent(go, *t) : nullptr;
      for (u32 f = 0; c && f < t->fieldCount; ++f)
        if (t->fields[f].type == FieldType::UnityEvent)
          reinterpret_cast<const UnityEvent*>(static_cast<const u8*>(static_cast<const void*>(c)) + t->fields[f].offset)->validate(*this, go->name);
    }
  }
  if (wake) {
    --cloneDepth_;
    if (ok)
      for (u32 k = 0; k < count; ++k)
        if (!objects_[map[k]].transform.parent_) awakeSubtree(&objects_[map[k]]);
  }
  arena_->rewind(mark);
  if (!ok) return false;

  if (out) {
    out->meshes.meshes = meshes;
    out->meshes.meshCount = meshCount;
    out->meshes.vertices = verts;
    out->meshes.vertexCount = vertCount;
    out->meshes.ext = pack.records<PackMeshExt>(pack_section::MSHX);  // nullptr = float vertices (every pack today)
    out->meshes.lit = pack.records<PackMeshLit>(pack_section::MSHL);  // nullptr = every mesh is baked
    out->meshes.vtx3 = pack.section(pack_section::VTX3);
    if (const PackSection* v3 = pack.find(pack_section::VTX3)) out->meshes.vtx3Size = v3->size;
    out->meshes.batches = pack.records<PackBoneBatch>(pack_section::BTCH, &out->meshes.batchCount);
    out->inputs = inputs;
    out->inputCount = inputCount;
    out->hasTarget = ctgt != nullptr;
    if (ctgt) out->target = *ctgt;
    out->assets = pack.assets();
    out->objectCount = count;
    out->componentCount = compCount;
  }
  return true;
}

}  // namespace rensga
