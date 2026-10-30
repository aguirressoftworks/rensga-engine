// F3.02: the data behind the VRAM map panel. Items come from the ConsoleTarget (color and depth buffers) and from the
// cooked size of every texture; the TargetModule places them (or the generic linear layout does).
// No ImGui, no console names.
#pragma once
#include <vector>

#include "rensga/editor/file_watcher.h"
#include "rensga/editor/target_module.h"

namespace rensga {
namespace editor {

// Framebuffers first, then every texture (and its palette) in path order. Imports textures that have no outcome yet.
std::vector<VramItem> collectVramItems(const ConsoleTargetData& ct, AssetSync& sync);
// `module` may be null (the project's target has no TargetModule): the generic linear layout is used.
VramLayout layoutVram(const TargetModule* module, const ConsoleTargetData& ct, const std::vector<VramItem>& items);

}  // namespace editor
}  // namespace rensga
