#include "runtime_boot.h"

#include "rensga/core/input.h"
#include "rensga/core/log.h"
#include "rensga/core/save.h"
#include "rensga/core/scene_manager.h"

namespace rensga {

u32 uploadPackTextures(IRenderer& renderer, const PackAssets& assets, u32* handles, u32 capacity) {
  const u32 n = assets.textureCount < capacity ? assets.textureCount : capacity;
  for (u32 i = 0; i < n; ++i) {
    const PackTexRec& t = assets.textures[i];
    TextureDesc d;
    d.width = t.width;
    d.height = t.height;
    d.format = static_cast<u8>(t.format);
    d.pixels = assets.texturePixels(t);
    d.palette = assets.texturePalette(t);
    d.storedWidth = t.storedWidth;
    d.storedHeight = t.storedHeight;
    d.flags = t.flags;
    handles[i] = renderer.uploadTexture(d);
    if (!handles[i]) RLOG_ERROR("game", "rensga: texture %u upload failed", i);
  }
  return n;
}

bool bootPack(const BootDeps& deps, BootState& st) {
  if (!deps.fs || !deps.arena || !deps.world) {
    RLOG_ERROR("game", "rensga: bootPack needs a file system, an arena and a world");
    return false;
  }
  if (deps.renderer && (!deps.textureHandles || !deps.textureHandleCapacity)) {
    RLOG_ERROR("game", "rensga: bootPack with a renderer needs textureHandles");
    return false;
  }
  st = BootState();
  Span<const u8> bytes = deps.fs->readFile(deps.packPath, *deps.arena);
  if (!bytes || !st.view.open(bytes.data, bytes.size, deps.packPath)) {
    RLOG_ERROR("game", "rensga: cannot read pack %s", deps.packPath);
    return false;
  }
  st.target = st.view.records<PackConsoleTarget>(pack_section::CTGT);
  st.width = deps.defaultWidth;
  st.height = deps.defaultHeight;
  if (st.target && st.target->width && st.target->height) {
    st.width = st.target->width;
    st.height = st.target->height;
  }
  if (deps.renderer) {
    if (deps.initRenderer && !deps.renderer->init(st.width, st.height)) {
      RLOG_ERROR("game", "rensga: renderer init failed (%ux%u)", st.width, st.height);
      return false;
    }
    st.rendererReady = true;
  }
  if (deps.onResolution) deps.onResolution(deps.onResolutionUser, st.width, st.height);

  if (deps.startScene) {
    SceneManager::bind(&st.view, 0);
    const i32 si = SceneManager::sceneIndexByName(deps.startScene);
    if (si < 0) {
      RLOG_ERROR("game", "rensga: no scene named %s in the pack", deps.startScene);
      return false;
    }
    st.sceneIndex = static_cast<u32>(si);
  }
  if (!deps.world->loadPack(st.view, &st.loaded, st.sceneIndex)) {
    RLOG_ERROR("game", "rensga: cannot load pack %s", deps.packPath);
    return false;
  }
  Input::loadActions(st.view, st.loaded.inputs, st.loaded.inputCount);
  const PackAssets& as = st.loaded.assets;
  static u32 noTextures[GfxAssets::kMaxTextures] = {};  // headless: handles stay 0, 2D components still find their records
  const u32* handles = noTextures;
  u32 cap = GfxAssets::kMaxTextures;
  if (deps.renderer) {
    deps.renderer->setMeshes(st.loaded.meshes);
    handles = deps.textureHandles;
    if (deps.textureHandleCapacity < cap) cap = deps.textureHandleCapacity;
    uploadPackTextures(*deps.renderer, as, deps.textureHandles, cap);
  }
  st.textureCount = as.textureCount < cap ? as.textureCount : cap;
  if (deps.renderer && as.textureCount) RLOG_INFO("game", "rensga: uploaded %u textures", static_cast<unsigned>(st.textureCount));
  GfxAssets::bind(&st.view, &st.loaded.assets, handles, st.textureCount);  // 2D components resolve textures here
  SceneManager::bind(&st.view, st.sceneIndex);
  if (deps.saves) Save::use(deps.saves);
  if (deps.audio) {
    deps.audio->init(deps.fs, st.target, deps.audioClock);
    deps.audio->registerPackClips(as);
  }
  return true;
}

}  // namespace rensga
