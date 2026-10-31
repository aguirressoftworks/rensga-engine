// F4.04 (agent "gu"): VRAM allocation for GuRenderer (fb0 0x000000, fb1 0x044000, depth 0x088000, textures first-fit from
// 0x0CC000 to 0x200000). Must match editor/targets/psp/src/psp_vram.cpp. No PSP headers: also compiled into the host test.
#include "rensga/render/gu_vram.h"

#include "rensga/core/backends.h"

namespace rensga {

namespace {
u32 roundUp16(u32 v) { return (v + 15u) & ~15u; }
}

void GuVramAllocator::init(u32 begin, u32 end) {
  begin_ = roundUp16(begin);
  end_ = end & ~15u;
  used_ = 0;
  count_ = 0;
  if (end_ > begin_) free_[count_++] = Range{begin_, end_ - begin_};
}

bool GuVramAllocator::alloc(u32 bytes, u32* offset) {
  const u32 size = roundUp16(bytes ? bytes : 1);
  for (u32 i = 0; i < count_; ++i) {
    if (free_[i].size < size) continue;
    *offset = free_[i].off;
    free_[i].off += size;
    free_[i].size -= size;
    if (free_[i].size == 0) {
      for (u32 j = i + 1; j < count_; ++j) free_[j - 1] = free_[j];
      --count_;
    }
    used_ += size;
    return true;
  }
  return false;
}

void GuVramAllocator::free(u32 offset, u32 bytes) {
  const u32 size = roundUp16(bytes ? bytes : 1);
  if (offset < begin_ || offset + size > end_ || size > used_) return;
  u32 at = 0;  // keep the list sorted by offset
  while (at < count_ && free_[at].off < offset) ++at;
  const bool mergePrev = at > 0 && free_[at - 1].off + free_[at - 1].size == offset;
  const bool mergeNext = at < count_ && offset + size == free_[at].off;
  used_ -= size;
  if (mergePrev && mergeNext) {
    free_[at - 1].size += size + free_[at].size;
    for (u32 j = at + 1; j < count_; ++j) free_[j - 1] = free_[j];
    --count_;
  } else if (mergePrev) {
    free_[at - 1].size += size;
  } else if (mergeNext) {
    free_[at].off = offset;
    free_[at].size += size;
  } else if (count_ < kMaxFree) {
    for (u32 j = count_; j > at; --j) free_[j] = free_[j - 1];
    free_[at] = Range{offset, size};
    ++count_;
  }  // else: the free list is full and the block is lost until reset() (cannot happen with <= 255 textures and merging)
}

u32 GuVramAllocator::largestFree() const {
  u32 best = 0;
  for (u32 i = 0; i < count_; ++i)
    if (free_[i].size > best) best = free_[i].size;
  return best;
}

u32 guTextureBytes(u8 format, u32 w, u32 h) {
  switch (format) {
    case tex_format::CLUT4: return w * h / 2;
    case tex_format::CLUT8: return w * h;
    case tex_format::RGBA5551:
    case tex_format::RGBA4444: return w * h * 2;
    default: return w * h * 4;
  }
}

}  // namespace rensga
