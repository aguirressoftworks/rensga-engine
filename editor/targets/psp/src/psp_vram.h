// PSP VRAM layout for the editor's VRAM map (Window > VRAM Map). Must match engine/backends/render/gu (gu_vram.cpp).
#pragma once
#include <vector>

#include "rensga/editor/target_module.h"

namespace rensga {
namespace editor {

VramLayout pspVramLayout(const ConsoleTargetData& ct, const std::vector<VramItem>& items);

}  // namespace editor
}  // namespace rensga
