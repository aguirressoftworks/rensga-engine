#include "rensga/core/memory.h"

#include <stdlib.h>
#if defined(__PSP__) || defined(__ANDROID__)  // aligned_alloc needs API 28; memalign is older
#include <malloc.h>
#endif

#include "rensga/core/log.h"
#if defined(_WIN32)
#include <malloc.h>
#endif

namespace rensga {

namespace {
// Aligned heap block, freed with free(). Used at startup (Arena::create) and by operator new.
void* alignedAlloc(size_t size, size_t align) {
  if (align < kDmaAlignment) align = kDmaAlignment;
  size = alignUp(size, align);
#if defined(__PSP__) || defined(__ANDROID__)  // aligned_alloc needs API 28; memalign is older
  return memalign(align, size);
#elif defined(_WIN32)
  return _aligned_malloc(size, align);
#else
  return aligned_alloc(align, size);
#endif
}
void freeBlock(void* p) {
#if defined(_WIN32)
  _aligned_free(p);
#else
  free(p);
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
  if (owns_ && base_) freeBlock(base_);
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

// ---- MemoryBudget ----------------------------------------------------------

void MemoryBudget::init(size_t totalBytes, size_t reservedBytes) {
  *this = MemoryBudget();
  total_ = totalBytes;
  reserved_ = reservedBytes > totalBytes ? totalBytes : reservedBytes;
}

bool MemoryBudget::createArena(Arena& arena, const char* name, size_t bytes) {
  bytes = alignUp(bytes, kDmaAlignment);
  if (count_ >= kMaxArenas) {
    lastRefused_ = name;
    RLOG_ERROR("memory", "arena '%s' refused: budget table is full (%u arenas)", name, kMaxArenas);
    return false;
  }
  if (bytes > remaining()) {
    lastRefused_ = name;
    RLOG_ERROR("memory", "arena '%s' refused: needs %zu bytes, only %zu left of the %zu byte budget (%zu reserved, %zu committed)",
               name, bytes, remaining(), total_, reserved_, committed_);
    return false;
  }
  if (!arena.create(bytes)) {
    lastRefused_ = name;
    RLOG_ERROR("memory", "arena '%s' refused: the system could not provide %zu bytes", name, bytes);
    return false;
  }
  committed_ += bytes;
  entries_[count_].name = name;
  entries_[count_].arena = &arena;
  ++count_;
  return true;
}

bool MemoryBudget::track(const Arena& arena, const char* name) {
  if (count_ >= kMaxArenas) return false;
  entries_[count_].name = name;
  entries_[count_].arena = &arena;
  ++count_;
  return true;
}

bool MemoryBudget::stats(u32 index, ArenaStats& out) const {
  if (index >= count_) return false;
  const Arena& a = *entries_[index].arena;
  out.name = entries_[index].name;
  out.capacity = a.capacity();
  out.used = a.used();
  out.peak = a.highWater();
  return true;
}

// ---- Heap counter ---------------------------------------------------------

namespace {
u64 g_allocCount = 0;
u64 g_totalBytes = 0;
bool g_guardEnabled = true;
u64 g_guardStart = 0;
u32 g_violations = 0;
bool g_inFrame = false;
u64 g_checkFrames = 0;
u64 g_checkBase = 0;

void* countedAlloc(size_t size, size_t align) {
  if (size == 0) size = 1;
#if defined(_WIN32)
  void* p = alignedAlloc(size, align);  // every block is freed with _aligned_free
#else
  void* p = align <= alignof(max_align_t) ? malloc(size) : alignedAlloc(size, align);
#endif
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
void noteFrameDone() {
  if (++g_checkFrames == 1) g_checkBase = g_allocCount;
}
u64 checkFrames() { return g_checkFrames; }
u64 checkAllocs() { return g_checkFrames ? g_allocCount - g_checkBase : 0; }
void checkReset() { g_checkFrames = 0; g_checkBase = 0; }
u64 logAllocCheck() {
  u64 k = checkAllocs();
  RLOG_INFO("game", "rensga: alloc-check frames=%llu allocs=%llu", static_cast<unsigned long long>(g_checkFrames),
            static_cast<unsigned long long>(k));
  return k;
}
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
void operator delete(void* p) noexcept { rensga::freeBlock(p); }
void operator delete[](void* p) noexcept { rensga::freeBlock(p); }
void operator delete(void* p, size_t) noexcept { rensga::freeBlock(p); }
void operator delete[](void* p, size_t) noexcept { rensga::freeBlock(p); }
void operator delete(void* p, std::align_val_t) noexcept { rensga::freeBlock(p); }
void operator delete[](void* p, std::align_val_t) noexcept { rensga::freeBlock(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { rensga::freeBlock(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { rensga::freeBlock(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { rensga::freeBlock(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { rensga::freeBlock(p); }
