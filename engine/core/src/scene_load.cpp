// F6 (A3, API 9): additive and incremental scene loading for World. loadPack (pack.cpp) stays the one-shot path; this file builds a
// scene over several frames:
//   beginSceneLoad  checks that the scene fits (object slots, component pool slots, arena bytes), creates the pools it needs and the
//                   scene-local index map (one array per world, kept for the run);
//   stepSceneLoad   creates objects, links parents, adds components, N work items per call. Objects are INACTIVE and no component is
//                   awake until the end, so nothing draws, updates or collides half built;
//   finishSceneLoad restores each object's active flag and wakes the roots, exactly like loadPack on a running world.
// No heap: the map and any new pools come from the arena. cloneDepth_ is raised only inside a step, so a script that calls
// AddComponent between two steps still gets Awake.
#include <stdio.h>
#include <string.h>

#include "rensga/core/components.h"
#include "rensga/core/log.h"
#include "rensga/core/unity_event.h"
#include "rensga/core/world.h"

namespace rensga {

namespace {
void say(char* why, u32 size, const char* fmt, const char* a, unsigned b, unsigned c) {
  if (why && size) snprintf(why, size, fmt, a, b, c);
}

struct SceneRange {
  const PackObject* objs = nullptr;
  const PackTransform* xfrms = nullptr;
  const PackComponent* comps = nullptr;
  const PackMeshRec* meshes = nullptr;
  const u8* fdat = nullptr;
  u32 compCount = 0, meshCount = 0, first = 0, count = 0;
};

bool rangeOf(const PackView& pack, u32 sceneIndex, SceneRange& r) {
  u32 objCount = 0, xfrmCount = 0;
  r.objs = pack.records<PackObject>(pack_section::OBJS, &objCount);
  r.xfrms = pack.records<PackTransform>(pack_section::XFRM, &xfrmCount);
  r.comps = pack.records<PackComponent>(pack_section::COMP, &r.compCount);
  r.meshes = pack.records<PackMeshRec>(pack_section::MESH, &r.meshCount);
  r.fdat = pack.section(pack_section::FDAT);
  if (!r.objs || xfrmCount != objCount) return false;
  u32 sceneCount = 0;
  const PackSceneRec* scenes = pack.records<PackSceneRec>(pack_section::SCNE, &sceneCount);
  r.first = 0;
  r.count = objCount;
  if (scenes) {
    if (sceneIndex >= sceneCount) return false;
    r.first = scenes[sceneIndex].firstObject;
    r.count = scenes[sceneIndex].objectCount;
    if (r.first > objCount || r.count > objCount - r.first) return false;
  } else if (sceneIndex != 0) {
    return false;
  }
  return true;
}
}  // namespace

bool World::sceneFits(const PackView& pack, u32 sceneIndex, char* why, u32 whySize) const {
  SceneRange r;
  if (!rangeOf(pack, sceneIndex, r)) {
    if (why && whySize) snprintf(why, whySize, "scene %u is not in the pack", sceneIndex);
    return false;
  }
  if (r.count > config_.maxObjects - liveObjects_) {
    say(why, whySize, "%s: %u objects, only %u free object slots", "scene", r.count, config_.maxObjects - liveObjects_);
    return false;
  }
  u32 need[kMaxTypes] = {};
  for (u32 i = 0; i < r.compCount; ++i) {
    if (r.comps[i].objectIndex < r.first || r.comps[i].objectIndex - r.first >= r.count) continue;
    const TypeInfo* t = TypeRegistry::instance().findByHash(r.comps[i].typeHash);
    if (!t) {
      if (why && whySize) snprintf(why, whySize, "unknown component type hash 0x%08x", static_cast<unsigned>(r.comps[i].typeHash));
      return false;
    }
    ++need[t->id];
  }
  size_t bytes = sceneMap_ ? 0 : sizeof(u32) * static_cast<size_t>(config_.maxObjects) + 16;
  for (u32 id = 0; id < kMaxTypes; ++id) {
    if (!need[id]) continue;
    const ComponentPool& p = pools_[id];
    if (p.type) {
      const u32 freeSlots = p.freeCount + (p.capacity - p.highWater);
      if (need[id] > freeSlots) {
        say(why, whySize, "component pool '%s' needs %u slots, %u free", p.type->name, need[id], freeSlots);
        return false;
      }
    } else {
      const TypeInfo* t = TypeRegistry::instance().byId(id);
      u32 cap = need[id] + config_.spareComponents;
      if (cap > config_.maxComponentsPerType) cap = config_.maxComponentsPerType;
      if (cap < need[id]) cap = need[id];
      const size_t stride = alignUp(t->size, kDmaAlignment);
      bytes += stride * cap + static_cast<size_t>(cap) * 9 + 64;
    }
  }
  if (bytes > arena_->remaining()) {
    say(why, whySize, "%s: needs about %u bytes of arena, %u left", "scene", static_cast<unsigned>(bytes), static_cast<unsigned>(arena_->remaining()));
    return false;
  }
  return true;
}

bool World::beginSceneLoad(const PackView& pack, u32 sceneIndex, SceneLoadJob& job, char* why, u32 whySize) {
  job = SceneLoadJob();
  if (!sceneFits(pack, sceneIndex, why, whySize)) return false;
  SceneRange r;
  rangeOf(pack, sceneIndex, r);
  u32 need[kMaxTypes] = {};
  for (u32 i = 0; i < r.compCount; ++i) {
    if (r.comps[i].objectIndex < r.first || r.comps[i].objectIndex - r.first >= r.count) continue;
    const TypeInfo* t = TypeRegistry::instance().findByHash(r.comps[i].typeHash);
    if (!r.fdat || !t) return false;
    ++need[t->id];
  }
  for (u32 id = 0; id < kMaxTypes; ++id) {
    if (!need[id] || pools_[id].type) continue;
    u32 cap = need[id] + config_.spareComponents;
    if (cap > config_.maxComponentsPerType) cap = config_.maxComponentsPerType;
    if (cap < need[id]) cap = need[id];
    if (!reservePool(*TypeRegistry::instance().byId(id), cap)) {
      if (why && whySize) snprintf(why, whySize, "cannot create a pool for '%s'", TypeRegistry::instance().byId(id)->name);
      return false;
    }
  }
  if (!sceneMap_) {
    sceneMap_ = arena_->allocateArray<u32>(config_.maxObjects ? config_.maxObjects : 1);
    if (!sceneMap_) {
      if (why && whySize) snprintf(why, whySize, "no arena memory for the scene index map");
      return false;
    }
  }
  job.pack = &pack;
  job.scene = sceneIndex;
  job.first = r.first;
  job.count = r.count;
  u32 mine = 0;  // components of this scene only: the progress denominator
  for (u32 i = 0; i < r.compCount; ++i)
    if (r.comps[i].objectIndex >= r.first && r.comps[i].objectIndex - r.first < r.count) ++mine;
  job.total = 2 * r.count + mine + 1;
  job.active = true;
  return true;
}

bool World::stepSceneLoad(SceneLoadJob& job, u32 maxItems) {
  if (!job.active) return false;
  if (job.phase >= 4) return true;
  SceneRange r;
  if (!rangeOf(*job.pack, job.scene, r)) {
    abortSceneLoad(job);
    return false;
  }
  const PackView& pack = *job.pack;
  ++cloneDepth_;
  u32 budget = maxItems ? maxItems : 1;
  while (budget > 0 && job.phase < 4) {
    if (job.phase == 0) {
      if (job.cursor >= job.count) { job.phase = 1; job.cursor = 0; continue; }
      const u32 k = job.cursor++;
      const u32 i = job.first + k;
      GameObject* go = createObject(pack.string(r.objs[i].nameStr));
      if (!go) {
        --cloneDepth_;
        RLOG_ERROR("scene", "incremental load of scene %u: object capacity exhausted", job.scene);
        abortSceneLoad(job);
        return false;
      }
      go->guid = r.objs[i].guid;
      go->sceneIndex = static_cast<i16>(job.scene);
      go->selfActive_ = false;  // restored by finishSceneLoad: nothing of a half built scene is active
      go->layer = r.objs[i].layer;
      if (pack.header->version >= 2 && r.objs[i].tag != 0) {
        const char* tag = pack.stringByIndex(r.objs[i].tag);
        if (*tag) go->SetTag(tag);
      }
      go->transform.lpos_ = Vector3(r.xfrms[i].position[0], r.xfrms[i].position[1], r.xfrms[i].position[2]);
      go->transform.lrot_ = Quaternion(r.xfrms[i].rotation[0], r.xfrms[i].rotation[1], r.xfrms[i].rotation[2], r.xfrms[i].rotation[3]);
      go->transform.lscale_ = Vector3(r.xfrms[i].scale[0], r.xfrms[i].scale[1], r.xfrms[i].scale[2]);
      sceneMap_[k] = go->index;
      ++job.done;
      --budget;
    } else if (job.phase == 1) {
      if (job.cursor >= job.count) { job.phase = 2; job.cursor = 0; continue; }
      const u32 k = job.cursor++;
      const i32 p = r.objs[job.first + k].parentIndex;
      if (p >= static_cast<i32>(job.first) && static_cast<u32>(p) < job.first + job.count)
        objects_[sceneMap_[k]].transform.setParent(&objects_[sceneMap_[static_cast<u32>(p) - job.first]].transform);
      ++job.done;
      --budget;
    } else if (job.phase == 2) {
      if (job.cursor >= r.compCount) { job.phase = 3; job.cursor = 0; continue; }
      const u32 i = job.cursor++;
      if (r.comps[i].objectIndex < job.first || r.comps[i].objectIndex - job.first >= job.count) continue;  // another scene's: free
      ++job.done;
      --budget;
      const TypeInfo* t = TypeRegistry::instance().findByHash(r.comps[i].typeHash);
      GameObject* go = &objects_[sceneMap_[r.comps[i].objectIndex - job.first]];
      Component* c = t ? addComponent(go, *t) : nullptr;
      if (!c || !readBlob(*t, c, r.fdat + r.comps[i].fieldOffset, r.comps[i].fieldSize)) {
        --cloneDepth_;
        RLOG_ERROR("scene", "incremental load of scene %u: component '%s' on '%s' failed", job.scene, t ? t->name : "?", go->name);
        abortSceneLoad(job);
        return false;
      }
    } else {  // phase 3: mesh links, in one go (a few records)
      for (u32 i = 0; i < r.meshCount; ++i) {
        if (r.meshes[i].objectIndex < job.first || r.meshes[i].objectIndex - job.first >= job.count) continue;
        GameObject* go = &objects_[sceneMap_[r.meshes[i].objectIndex - job.first]];
        if (MeshRenderer* mr = getComponent<MeshRenderer>(go)) mr->meshIndex = i;
        else if (SkinnedMeshRenderer* sr = getComponent<SkinnedMeshRenderer>(go)) sr->meshIndex = i;
      }
      job.phase = 4;
    }
  }
  --cloneDepth_;
  return job.phase >= 4;
}

bool World::finishSceneLoad(SceneLoadJob& job) {
  if (!job.active || job.phase < 4) return false;
  SceneRange r;
  if (!rangeOf(*job.pack, job.scene, r)) {
    abortSceneLoad(job);
    return false;
  }
  for (u32 k = 0; k < job.count; ++k) objects_[sceneMap_[k]].selfActive_ = r.objs[job.first + k].active != 0;
  // UnityEvent targets are looked up by GUID at fire time; warn now about the ones that cannot resolve (as loadPack does).
  for (u32 i = 0; i < r.compCount; ++i) {
    if (r.comps[i].objectIndex < job.first || r.comps[i].objectIndex - job.first >= job.count) continue;
    const TypeInfo* t = TypeRegistry::instance().findByHash(r.comps[i].typeHash);
    GameObject* go = &objects_[sceneMap_[r.comps[i].objectIndex - job.first]];
    Component* c = t ? getComponent(go, *t) : nullptr;
    for (u32 f = 0; c && f < t->fieldCount; ++f)
      if (t->fields[f].type == FieldType::UnityEvent)
        reinterpret_cast<const UnityEvent*>(static_cast<const u8*>(static_cast<const void*>(c)) + t->fields[f].offset)->validate(*this, go->name);
  }
  for (u32 k = 0; k < job.count; ++k) {
    GameObject* go = &objects_[sceneMap_[k]];
    if (!go->transform.parent_) awakeSubtree(go);
  }
  job.active = false;
  return true;
}

void World::abortSceneLoad(SceneLoadJob& job) {
  if (!job.active) return;
  const i32 scene = static_cast<i32>(job.scene);
  job.active = false;
  destroyScene(scene);
}

u32 World::destroyScene(i32 sceneIndex) {
  u32 n = 0;
  for (u32 i = 0; i < objectHighWater_; ++i) {
    GameObject* go = &objects_[i];
    if (!go->alive || go->dying || go->transform.parent_ || go->dontDestroyOnLoad || go->sceneIndex != sceneIndex) continue;
    destroy(go);
    ++n;
  }
  flushNow();
  return n;
}

u32 World::sceneObjectCount(i32 sceneIndex) const {
  u32 n = 0;
  for (u32 i = 0; i < objectHighWater_; ++i)
    if (objects_[i].alive && !objects_[i].dying && objects_[i].sceneIndex == sceneIndex) ++n;
  return n;
}

}  // namespace rensga
