// F4.04: PSP video-memory map and the first-fit texture allocator. Plain C++ (no PSP headers) so tests/render checks it
// on the host. The layout is a contract with editor/targets/psp/src/psp_vram.cpp (the editor's VRAM map).
#pragma once
#include "rensga/core/types.h"

namespace rensga {

// VRAM is 2 MiB. Two 480x272 color buffers at a 512-pixel stride, then a 16-bit depth buffer, then the texture area.
namespace gu_vram {
constexpr u32 kStride = 512;
constexpr u32 kScreenW = 480, kScreenH = 272;
constexpr u32 kTotal = 0x200000;
// Color buffer format. 5650 is 2 bytes per pixel: fb0 0x000000, fb1 0x044000, depth 0x088000, textures 0x0CC000.
constexpr u32 kFbBytesPerPixel = 2;
constexpr u32 kFbBytes = kStride * kScreenH * kFbBytesPerPixel;
constexpr u32 kDepthBytes = kStride * kScreenH * 2;
constexpr u32 kFb0 = 0;
constexpr u32 kFb1 = kFbBytes;
constexpr u32 kDepth = kFbBytes * 2;
constexpr u32 kTexBegin = kDepth + kDepthBytes;
constexpr u32 kTexEnd = kTotal;
constexpr u32 kTexArea = kTexEnd - kTexBegin;
constexpr u32 kAlign = 16;  // textures, palettes and buffers are 16-byte aligned
}  // namespace gu_vram

// First-fit allocator over [begin, end). No LRU and no eviction: a block lives until free() or reset(). Sizes are
// rounded up to 16 bytes, so every block stays 16-byte aligned. Fixed storage, no heap.
class GuVramAllocator {
 public:
  static constexpr u32 kMaxFree = 64;
  void init(u32 begin, u32 end);
  void reset() { init(begin_, end_); }
  // Returns true and the byte offset of the first free range that fits.
  bool alloc(u32 bytes, u32* offset);
  // Gives a block back; neighbouring free ranges merge. `bytes` must be what was passed to alloc.
  void free(u32 offset, u32 bytes);
  u32 used() const { return used_; }
  u32 area() const { return end_ - begin_; }
  u32 largestFree() const;
  u32 freeRanges() const { return count_; }

 private:
  struct Range { u32 off, size; };
  Range free_[kMaxFree];
  u32 count_ = 0, begin_ = 0, end_ = 0, used_ = 0;
};

// Stored size of a PSP texture in bytes (power-of-two stored size already applied by the cooker), per format.
u32 guTextureBytes(u8 format, u32 storedW, u32 storedH);

}  // namespace rensga
