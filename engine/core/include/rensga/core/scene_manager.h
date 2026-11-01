// F2.05: SceneManager. Scenes are the pack's SCNE list (build-settings order); a pack without SCNE has one scene.
// LoadScene only records the request. GameLoop::frame performs it at the END of the frame (after the destroy flush),
// so scripts can call it from Update or a trigger without invalidating what the rest of the frame touches.
// Loading destroys every object of the old scene except roots marked with DontDestroyOnLoad (rensga/Object.h),
// creates the new scene's objects, and wakes them (Awake/OnEnable now, Start before their first Update).
#pragma once
#include "rensga/core/pack_format.h"
#include "rensga/core/types.h"

namespace rensga {

class World;

// ---- F6 contract (C0; behaviour by A3, API 9) --------------------------------------------------------------------------
// Additive and asynchronous loading. The declarations below are final; until A3 lands they are stubs in
// engine/api/src/scene_api.cpp: LoadScene(..., Single) behaves like the plain call, Additive and the async calls fail safely
// (false / an invalid AsyncOperation) and log once.
//
// Rules A3 implements (fixed here so Stage 1 agents can rely on them):
//  - Scenes are the pack's SCNE records (the "scenes in build"). An additive load creates the scene's objects next to the loaded
//    ones; every scene occupies a contiguous object range (PackSceneRec). The active scene is the one new objects belong to and the
//    one Scene::buildIndex() reports for GetActiveScene; Single loads replace every loaded scene except DontDestroyOnLoad roots.
//  - Everything is deferred to the end of the frame like LoadScene. LoadSceneAsync spreads the work over frames (progress 0..0.9
//    while loading, 1 when active) and checks the arena budget first; with allowSceneActivation false it stops at 0.9.
//  - sceneLoaded callbacks run after Awake/OnEnable of the new objects, before their first Start, in registration order.
enum class LoadSceneMode : u8 { Single, Additive };

// A handle to a scene of the build. buildIndex is the SCNE index; a scene that is not loaded reports isLoaded() == false.
struct Scene {
  i32 index = -1;  // SCNE index, -1 = invalid scene
  bool IsValid() const { return index >= 0; }
  i32 buildIndex() const { return index; }
  const char* name() const;  // "" when invalid
  bool isLoaded() const;
  bool operator==(const Scene& o) const { return index == o.index; }
  bool operator!=(const Scene& o) const { return index != o.index; }
};

// Result of LoadSceneAsync / UnloadSceneAsync. A small fixed table (kMaxAsyncOperations) holds the state, so the handle is
// two integers and nothing allocates. Valid handles read progress()/isDone() until the table slot is reused (4 newer operations).
struct AsyncOperation {
  static constexpr u32 kMaxAsyncOperations = 4;
  u32 slot = 0xFFFFFFFFu;
  u32 serial = 0;
  bool IsValid() const;
  Scalar progress() const;  // 0..1
  bool isDone() const;
  bool allowSceneActivation() const;  // default true
  void setAllowSceneActivation(bool allow) const;
};

struct SceneManager {
  // The runtime main binds the pack after World::loadPack(view, ..., 0) loaded scene 0.
  static void bind(const PackView* pack, u32 loadedSceneIndex = 0);
  static void unbind();
  static u32 sceneCount();                     // SCNE records, 1 without SCNE
  static const char* sceneName(u32 index);     // "" if out of range
  static i32 sceneIndexByName(const char* name);  // -1 if unknown
  static i32 activeSceneIndex();
  static const char* activeSceneName();
  // Deferred to the end of the frame. False if the scene does not exist.
  static bool LoadScene(i32 buildIndex);
  static bool LoadScene(const char* name);
  static bool loadPending();

  // ---- F6 (stubs in engine/api/src/scene_api.cpp until A3) ----
  static bool LoadScene(i32 buildIndex, LoadSceneMode mode);
  static bool LoadScene(const char* name, LoadSceneMode mode);
  static AsyncOperation LoadSceneAsync(i32 buildIndex, LoadSceneMode mode = LoadSceneMode::Single);
  static AsyncOperation LoadSceneAsync(const char* name, LoadSceneMode mode = LoadSceneMode::Single);
  static AsyncOperation UnloadSceneAsync(i32 buildIndex);
  static AsyncOperation UnloadSceneAsync(const char* name);
  static Scene GetActiveScene();
  static bool SetActiveScene(Scene scene);  // false unless the scene is loaded
  static Scene GetSceneByName(const char* name);
  static Scene GetSceneByBuildIndex(i32 buildIndex);
  static u32 loadedSceneCount();
  static Scene GetLoadedSceneAt(u32 index);
  // Unity's SceneManager.sceneLoaded / sceneUnloaded: plain function pointers (no heap), up to 4 each. False when the table is full.
  using SceneLoadedCallback = void (*)(Scene scene, LoadSceneMode mode);
  using SceneUnloadedCallback = void (*)(Scene scene);
  static bool addSceneLoaded(SceneLoadedCallback cb);
  static void removeSceneLoaded(SceneLoadedCallback cb);
  static bool addSceneUnloaded(SceneUnloadedCallback cb);
  static void removeSceneUnloaded(SceneUnloadedCallback cb);
  // Called by GameLoop::frame after the frame. Returns true if a scene was loaded.
  static bool processPending(World& world);

  // ---- F6 (A3) additions ----
  static const PackView* pack();                 // the bound pack (null before bind)
  static bool isSceneLoaded(i32 buildIndex);
  // Work items (objects, parent links, components) one async load step does per frame; default 32. A small value stretches a load
  // over more frames (loading screens, tests). 0 is treated as 1.
  static void setAsyncChunk(u32 items);
  static u32 asyncChunk();
  static u32 loadsCompleted();                   // scenes loaded since bind (any mode), for logs and tests
};

}  // namespace rensga
