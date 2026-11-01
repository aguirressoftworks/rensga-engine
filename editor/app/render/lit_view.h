// F6.02 (Editor 8): the lit Scene view and the edit-mode Game view. The open scene is cooked from memory into the edit-mode World
// (EditModeWorld / EditorWorld, the same path as Play in editor) and drawn with the same GlRenderer the player uses: textures, MATL
// materials, the scene's Light components (at most PreviewRules.maxLightsPerObject per object), fog, skinned meshes in their first
// pose, baked vertex colors. The Scene view then draws its grid, selection outline, markers and gizmos over that image.
// Nothing here names a console: the limits come from PreviewRules / ConsoleTarget data.
#pragma once
#include <string>

#include "rensga/editor/viewport.h"

namespace rensga {
namespace editor {

struct App;

enum class LitSlot { Scene = 0, Game = 1, Thumb = 2 };

// One lit frame of the open scene seen through the editor camera (`view`, `proj` in the editor's convention, see viewport.h), at
// w x h pixels. Returns a GL texture (bottom row first) or 0 when no World exists (scripts still compiling, the scene does not cook)
// or lit drawing is switched off; the caller then draws unlit. While Play in editor runs the running World is drawn instead.
unsigned litSceneTexture(App& a, LitSlot slot, int w, int h, const M4& view, const M4& proj);
// The scene as its Camera component sees it (the engine looks down the camera's local -z), at w x h pixels. 0 when unavailable.
unsigned litGameTexture(App& a, int w, int h);

// The scene seen by the Camera component of one object (Inspector thumbnail), w x h pixels. 0 when unavailable.
unsigned litCameraTexture(App& a, const std::string& objectGuid, int w, int h);

// Lit view on/off (Scene view toolbar "Lit"); on by default. --ui-event scenelit:on|off.
bool& litViewEnabled();
// What the last lit frame drew (tests, status line): objects, triangles, lights in the frame, lights per object limit, World id.
struct LitStats { unsigned draws = 0, lights = 0, maxLights = 0, objects = 0; bool fog = false; unsigned long long worldId = 0; unsigned frames = 0; };
const LitStats& litStats();

// Shared 5-6-5 / dither pass of the preview (defined in game_view.cpp).
unsigned previewPostProcess(unsigned src, int w, int h, bool quantize, bool dither, bool nearest);

}  // namespace editor
}  // namespace rensga
