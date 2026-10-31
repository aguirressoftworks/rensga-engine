// F4 contract (frozen): the one pack-boot sequence every runtime main shares (desktop/Android, headless, PS2, PSP, PSP sim).
//
// bootPack does, in this order:
//   1. fs->readFile(packPath, arena) and PackView::open
//   2. reads CTGT: width/height (else the defaults below), ramBytes etc. stay in BootState::target
//   3. renderer->init(width, height) when deps.renderer && deps.initRenderer, then deps.onResolution
//   4. resolves the start scene (SceneManager::bind + sceneIndexByName) and World::loadPack
//   5. Input::loadActions
//   6. renderer->setMeshes (MeshTable, incl. the optional MSHX extension) and uploadPackTextures (TEXR) when deps.renderer
//   7. GfxAssets::bind, SceneManager::bind(view, sceneIndex)
//   8. Save::use(deps.saves) when set
//   9. audio->init(fs, target, clock) + registerPackClips when deps.audio (music / stress stay with the main)
// Everything is allocation-free after the arena; it never calls exit() and reports failure through the return value.
#pragma once
#include "rensga/core/audio.h"
#include "rensga/core/backends.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/memory.h"
#include "rensga/core/save.h"
#include "rensga/core/world.h"
#include "runtime_audio.h"

namespace rensga {

// What the main provides. Pointers are borrowed and must outlive the BootState.
struct BootDeps {
  IFileSystem* fs = nullptr;  // required: reads packPath
  Arena* arena = nullptr;     // required: the pack bytes are read into it and live as long as the pack is in use
  World* world = nullptr;     // required: already init'd; bootPack calls World::loadPack on it
  const char* packPath = "data:/game.rpak";
  const char* startScene = nullptr;  // scene name from the pack's SCNE table; null = the first scene

  IRenderer* renderer = nullptr;  // null = no GPU (headless, sim): no init, no mesh/texture upload
  bool initRenderer = true;       // false when the main already called renderer->init (e.g. it needed the size first)
  // Receives the GPU handles of TEXR[i]; GfxAssets keeps this pointer, so it must outlive the pack. Required with a renderer.
  u32* textureHandles = nullptr;
  u32 textureHandleCapacity = 0;  // entries in textureHandles (GfxAssets::kMaxTextures at most are used)

  u32 defaultWidth = 480, defaultHeight = 272;  // virtual resolution when the pack has no CTGT
  // Called once with the final virtual resolution after the renderer is initialised (e.g. IPlatform touch mapping).
  void (*onResolution)(void* user, u32 width, u32 height) = nullptr;
  void* onResolutionUser = nullptr;

  ISaveStore* saves = nullptr;        // installed with Save::use; null leaves the current store
  RuntimeAudio* audio = nullptr;      // null = no audio
  AudioMixer::ClockUsFn audioClock = nullptr;  // microsecond clock handed to RuntimeAudio::init
};

// What bootPack hands back. `view` and `loaded` point into deps.arena: do not move or destroy them while the game runs.
struct BootState {
  PackView view;
  LoadedPack loaded;
  const PackConsoleTarget* target = nullptr;  // CTGT record, null if the pack has none
  u32 width = 0, height = 0;                  // virtual resolution in use
  u32 sceneIndex = 0;                         // scene that was loaded
  u32 textureCount = 0;                       // TEXR records bound (<= GfxAssets::kMaxTextures)
  bool rendererReady = false;                 // renderer->init succeeded (or the caller did it)
};

// Returns false after logging a "rensga: ..." error line; BootState is then unspecified.
bool bootPack(const BootDeps& deps, BootState& state);

// Uploads every TEXR record of `assets` (flags and stored size included) and writes the handles to `handles`
// (0 = failed upload). Returns the number of records processed. Used by bootPack and by hot reload.
u32 uploadPackTextures(IRenderer& renderer, const PackAssets& assets, u32* handles, u32 capacity);

}  // namespace rensga
