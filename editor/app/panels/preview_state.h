// F5.07 / F5.08: state shared by the Game view (preview mode) and the Inspector (validation list).
// No console names: every rule is data from the TargetModule / ConsoleTarget (effectivePreviewRules, effectiveImportRules).
#pragma once
#include <string>

#include "app.h"
#include "rensga/editor/editor_world.h"

namespace rensga {
namespace editor {

// How the Game view imitates the target device. Defaults come from the target's PreviewRules the first time a project is shown.
struct PreviewOptions {
  bool initialized = false;
  bool quantize = false;       // output quantized to 5-6-5 (PreviewRules.colorBits == 16)
  bool dither = false;         // ordered 4x4 dither before quantizing
  bool deviceFrame = false;    // bezel drawn around the screen
  bool safeArea = false;       // inset rectangle
  bool consoleTextures = true; // textures as the target stores them (palette banding, 16-bit colour, downscale)
  bool lightLimit = true;      // each object lit by at most PreviewRules.maxLightsPerObject lights
  bool bars = true;            // RAM / VRAM / polygon bars under the frame
};
PreviewOptions& previewOptions();

// The open scene cooked in the target's own form (budget usage, decoded textures, validation findings). Recooks when the scene
// document changed (at most once per second unless `force`). Null when the project's target has no cooker plugin or the cook failed.
// `err` (optional) receives why it is null.
const ConsolePreview* scenePreview(App& a, bool force = false, std::string* err = nullptr);

// --ui-event verbs of the preview / validation (called from inspectorHook, which main.cpp already dispatches to):
//   preview:<key>=<0|1|16|32>[,<key>=..]  keys: dither frame safe color textures lights bars   |  preview:reset
//   validate                  recook and print the findings ("validate: N finding(s) ..." and one "validate-item:" line each)
//   validatesel:<n>           select the object of finding n (as a click on it does)
//   previewdump               print the effective rules and options ("preview: ...")
bool previewHook(App& a, const std::string& act, const std::string& arg);

}  // namespace editor
}  // namespace rensga
