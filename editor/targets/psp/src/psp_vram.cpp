#include "psp_vram.h"

#include <utility>

namespace rensga {
namespace editor {

// F3.02: the PSP GU keeps its buffers at 512-pixel strides (draw buffer, display buffer, depth buffer, in that order);
// textures follow, rows padded to 16 bytes, each block 16-byte aligned. Palettes are 16-byte aligned too.
VramLayout pspVramLayout(const ConsoleTargetData& ct, const std::vector<VramItem>& items) {
  VramLayout l;
  l.capacity = ct.vramBytes;
  uint64_t at = 0;
  for (const VramItem& src : items) {
    VramItem it = src;
    uint64_t size = it.bytes;
    if (it.kind == "framebuffer" || it.kind == "depth") {
      size = 512ull * it.height * it.bits / 8;  // fixed 512-pixel stride
    } else if (it.kind == "texture" && it.width && it.height && it.bits) {
      const uint64_t pitch = (static_cast<uint64_t>(it.width) * it.bits / 8 + 15) / 16 * 16;
      size = pitch * it.height;
    }
    at = (at + 15) / 16 * 16;
    VramPlacement p;
    p.item = it;
    p.offset = at;
    p.size = size;
    at += size;
    p.outside = at > l.capacity;
    l.items.push_back(std::move(p));
  }
  l.used = at;
  return l;
}

}  // namespace editor
}  // namespace rensga
