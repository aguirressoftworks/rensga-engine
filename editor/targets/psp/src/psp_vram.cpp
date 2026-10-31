#include "psp_vram.h"

#include <utility>

namespace rensga {
namespace editor {

namespace {
constexpr uint64_t kStride = 512;  // pixels, color and depth buffers

uint64_t roundUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }
uint32_t pow2AtLeast(uint32_t v, uint32_t minimum) {
  uint32_t p = minimum;
  while (p < v) p <<= 1;
  return p;
}
}  // namespace

// F3.02 / F4.04: the PSP GU keeps its buffers at 512-pixel strides (draw buffer, display buffer, depth buffer, in that
// order); textures follow, each block 16-byte aligned. Palettes (16 or 256 RGBA8 entries) live in VRAM too.
VramLayout pspVramLayout(const ConsoleTargetData& ct, const std::vector<VramItem>& items) {
  VramLayout l;
  l.capacity = ct.vramBytes;
  uint64_t at = 0;
  for (const VramItem& src : items) {
    VramItem it = src;
    uint64_t size = it.bytes;
    if (it.kind == "framebuffer" || it.kind == "depth") {
      size = kStride * it.height * it.bits / 8;  // fixed 512-pixel stride
    } else if (it.kind == "texture" && it.width && it.height && it.bits) {
      // Stored size the cooker produces: power of two, with the minimum row width of the format.
      const uint32_t minW = it.bits == 4 ? 32 : (it.bits == 8 ? 16 : 8);
      const uint32_t sw = pow2AtLeast(it.width, minW), sh = pow2AtLeast(it.height, 8);
      size = static_cast<uint64_t>(sw) * sh * it.bits / 8;
    }
    at = roundUp(at, 16);
    VramPlacement p;
    p.item = it;
    p.offset = at;
    p.size = roundUp(size ? size : 1, 16);
    at += p.size;
    p.outside = at > l.capacity;
    l.items.push_back(std::move(p));
  }
  l.used = at;
  return l;
}

}  // namespace editor
}  // namespace rensga
