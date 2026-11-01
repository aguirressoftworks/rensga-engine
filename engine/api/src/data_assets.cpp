// F6.07: DataAssets::Get<T>(guid). Instances live in a fixed table (no heap). An entry keeps a copy of the cooked blob it was built
// from, so a different pack bound later (the editor plays the project again) rebuilds the instance instead of serving stale values.
#include <string.h>

#include "rensga/DataAsset.h"
#include "rensga/core/log.h"
#include "rensga/core/scene_manager.h"

namespace rensga {

namespace {
struct Entry {
  Guid guid;
  u32 typeHash = 0;
  u32 blobSize = 0;
  bool live = false;
  u8 blob[kDataAssetMaxBytes];
  alignas(16) u8 object[kDataAssetMaxBytes];
};
Entry g_entries[kDataAssetMaxLive];
Guid g_warned[8];  // guids already reported, so a miss in Update does not flood the log
u32 g_warnedCount = 0;

bool warnOnce(const Guid& g) {
  for (u32 i = 0; i < g_warnedCount; ++i)
    if (g_warned[i] == g) return false;
  if (g_warnedCount < 8) g_warned[g_warnedCount++] = g;
  return true;
}

// Reflected fields are trivially destructible, so nothing runs here. (The TypeInfo may even be gone: a hot-reloaded script module
// unregisters its types, so an entry never keeps a TypeInfo pointer.)
void release(Entry& e) { e.live = false; }
}  // namespace

const void* DataAssets::Find(const Guid& guid, const TypeInfo* type) {
  if (guid.isNull() || !type) return nullptr;
  const PackView* pack = SceneManager::pack();
  if (!pack) return nullptr;
  const PackContent c = pack->content();
  const PackDataAsset* d = c.findData(guid);
  if (!d) {
    if (warnOnce(guid)) RLOG_WARN("data", "DataAssets.Get<%s>: this GUID is not a data asset of the pack", type->name);
    return nullptr;
  }
  if (d->typeHash != type->hash) {
    if (warnOnce(guid)) RLOG_WARN("data", "DataAssets.Get<%s>: the asset is of another type", type->name);
    return nullptr;
  }
  if (d->blobSize > kDataAssetMaxBytes || type->size > kDataAssetMaxBytes || type->align > 16 || !type->construct) {
    if (warnOnce(guid)) RLOG_ERROR("data", "DataAssets.Get<%s>: larger than %u bytes", type->name, kDataAssetMaxBytes);
    return nullptr;
  }
  const u8* src = c.dataBlob(*d);
  Entry* free = nullptr;
  for (Entry& e : g_entries) {
    if (e.live && e.guid == guid) {
      if (e.typeHash == type->hash && e.blobSize == d->blobSize && memcmp(e.blob, src, d->blobSize) == 0) return e.object;
      free = &e;  // the pack changed under this guid: rebuild in place
      release(e);
      break;
    }
    if (!e.live && !free) free = &e;
  }
  if (!free) {
    if (warnOnce(guid)) RLOG_ERROR("data", "DataAssets.Get<%s>: all %u data asset slots are in use", type->name, kDataAssetMaxLive);
    return nullptr;
  }
  type->construct(free->object);
  if (!readBlob(*type, free->object, src, d->blobSize)) {
    if (warnOnce(guid)) RLOG_ERROR("data", "DataAssets.Get<%s>: the cooked data does not match the type", type->name);
    return nullptr;
  }
  free->guid = guid;
  free->typeHash = type->hash;
  free->blobSize = d->blobSize;
  memcpy(free->blob, src, d->blobSize);
  free->live = true;
  return free->object;
}

u32 DataAssets::count() {
  const PackView* pack = SceneManager::pack();
  return pack ? pack->content().dataCount : 0;
}

u32 DataAssets::liveCount() {
  u32 n = 0;
  for (const Entry& e : g_entries)
    if (e.live) ++n;
  return n;
}

}  // namespace rensga
