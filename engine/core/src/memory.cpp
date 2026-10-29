#include "rensga/core/memory.h"

#include <stdlib.h>
#if defined(__PSP__)
#include <malloc.h>
#endif

#include "rensga/core/log.h"

namespace rensga {

namespace {
// Aligned heap block, freed with free(). Used at startup (Arena::create) and by operator new.
void* alignedAlloc(size_t size, size_t align) {
  if (align < kDmaAlignment) align = kDmaAlignment;
  size = alignUp(size, align);
#if defined(__PSP__)
  return memalign(align, size);
#else
  return aligned_alloc(align, size);
#endif
}
}  // namespace

// ---- Arena ----------------------------------------------------------------

bool Arena::create(size_t capacity) {
  release();
  capacity = alignUp(capacity, kDmaAlignment);
  void* mem = alignedAlloc(capacity, kDmaAlignment);
  if (!mem) return false;
  base_ = static_cast<u8*>(mem);
  capacity_ = capacity;
  used_ = 0;
  highWater_ = 0;
  owns_ = true;
  return true;
}

void Arena::init(void* memory, size_t capacity) {
  release();
  base_ = static_cast<u8*>(memory);
  capacity_ = capacity;
  used_ = 0;
  highWater_ = 0;
  owns_ = false;
}

void Arena::release() {
  if (owns_ && base_) free(base_);
  base_ = nullptr;
  capacity_ = used_ = highWater_ = 0;
  owns_ = false;
}

void* Arena::allocate(size_t size, size_t alignment) {
  if (!base_) return nullptr;
  if (alignment < kDmaAlignment) alignment = kDmaAlignment;
  size_t start = alignUp(reinterpret_cast<uintptr_t>(base_) + used_, alignment) - reinterpret_cast<uintptr_t>(base_);
  size_t end = start + alignUp(size, kDmaAlignment);
  if (end > capacity_) {
    RLOG_ERROR("memory", "arena out of memory: need %zu, %zu of %zu used", size, used_, capacity_);
    return nullptr;
  }
  used_ = end;
  if (used_ > highWater_) highWater_ = used_;
  return base_ + start;
}

void Arena::rewind(size_t mark) {
  if (mark <= used_) used_ = mark;
}

MemoryConfig MemoryConfig::fromRamBudget(size_t ramBytes) {
  MemoryConfig c;
  size_t usable = ramBytes - ramBytes / 8;
  c.frameBytes = alignUp(usable / 16, kDmaAlignment);
  c.arenaBytes = alignUp(usable - c.frameBytes, kDmaAlignment);
  return c;
}

// ---- Heap counter ---------------------------------------------------------

namespace {
u64 g_allocCount = 0;
u64 g_totalBytes = 0;
bool g_guardEnabled = true;
u64 g_guardStart = 0;
u32 g_violations = 0;
bool g_inFrame = false;

void* countedAlloc(size_t size, size_t align) {
  if (size == 0) size = 1;
  void* p = align <= alignof(max_align_t) ? malloc(size) : alignedAlloc(size, align);
  if (p) {
    ++g_allocCount;
    g_totalBytes += size;
  }
  return p;
}
}  // namespace

namespace heap {
u64 allocationCount() { return g_allocCount; }
u64 totalBytes() { return g_totalBytes; }
}  // namespace heap

void FrameHeapGuard::begin() {
  g_guardStart = g_allocCount;
  g_inFrame = true;
}
bool FrameHeapGuard::end() {
  bool ok = true;
  if (g_inFrame && g_guardEnabled && g_allocCount != g_guardStart) {
    ++g_violations;
    RLOG_ERROR("memory", "frame failed: %llu heap allocation(s) during the frame",
               static_cast<unsigned long long>(g_allocCount - g_guardStart));
    ok = false;
  }
  g_inFrame = false;
  return ok;
}
u32 FrameHeapGuard::violations() { return g_violations; }
void FrameHeapGuard::resetViolations() { g_violations = 0; }
void FrameHeapGuard::setEnabled(bool e) { g_guardEnabled = e; }
bool FrameHeapGuard::enabled() { return g_guardEnabled; }

}  // namespace rensga

// Replaced global allocation functions: count every heap allocation.
void* operator new(size_t size) {
  void* p = rensga::countedAlloc(size, 0);
  if (!p) abort();
  return p;
}
void* operator new[](size_t size) {
  void* p = rensga::countedAlloc(size, 0);
  if (!p) abort();
  return p;
}
void* operator new(size_t size, std::align_val_t a) {
  void* p = rensga::countedAlloc(size, static_cast<size_t>(a));
  if (!p) abort();
  return p;
}
void* operator new[](size_t size, std::align_val_t a) {
  void* p = rensga::countedAlloc(size, static_cast<size_t>(a));
  if (!p) abort();
  return p;
}
void* operator new(size_t size, const std::nothrow_t&) noexcept { return rensga::countedAlloc(size, 0); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return rensga::countedAlloc(size, 0); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
void operator delete(void* p, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { free(p); }
