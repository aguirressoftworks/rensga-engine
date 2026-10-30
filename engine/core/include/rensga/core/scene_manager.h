// F2.05: SceneManager. Scenes are the pack's SCNE list (build-settings order); a pack without SCNE has one scene.
// LoadScene only records the request. GameLoop::frame performs it at the END of the frame (after the destroy flush),
// so scripts can call it from Update or a trigger without invalidating what the rest of the frame touches.
// Loading destroys every object of the old scene (there is no DontDestroyOnLoad yet; static script state survives),
// creates the new scene's objects, and wakes them (Awake/OnEnable now, Start before their first Update).
#pragma once
#include "rensga/core/pack_format.h"
#include "rensga/core/types.h"

namespace rensga {

class World;

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
  // Called by GameLoop::frame after the frame. Returns true if a scene was loaded.
  static bool processPending(World& world);
};

}  // namespace rensga
