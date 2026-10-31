// PSP VRAM layout for the editor's VRAM map (Window > VRAM Map). Must match engine/backends/render/gu (gu_vram.h).
//
// Layout of the GU backend (F4.04): both color buffers and the depth buffer use a 512-pixel stride. Color is 5650
// (2 bytes a pixel, ConsoleTarget framebuffer bpp 16): fb0 0x000000, fb1 0x044000, depth 0x088000, textures from 0x0CC000 to
// the end of the 2 MiB. Textures and palettes are placed first-fit in the order of the items. A texture's stored size is
// a power of two (at least 32 px wide for CLUT4, 16 for CLUT8, 8 otherwise, 8 rows), so that is what the map reserves.
#pragma once
#include <vector>

#include "rensga/editor/target_module.h"

namespace rensga {
namespace editor {

VramLayout pspVramLayout(const ConsoleTargetData& ct, const std::vector<VramItem>& items);

}  // namespace editor
}  // namespace rensga
