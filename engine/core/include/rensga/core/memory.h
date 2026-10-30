// MVP-08: arena + frame allocators and the per-frame heap counter.
#pragma once
#include <new>

#include "rensga/core/types.h"

namespace rensga {

// Linear allocator over one block. Never frees individual allocations.
class Arena {
 public:
  Arena() {}
  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  ~Arena() { release(); }

  // Take ownership of a new 16-byte aligned block from the heap (startup only).
  bool create(size_t capacity);
  // Use external memory (not owned). `memory` must be 16-byte aligned.
  void init(void* memory, size_t capacity);
  void release();

  void* allocate(size_t size, size_t alignment = kDmaAlignment);
  template <class T>
  T* allocateArray(size_t count, size_t alignment = kDmaAlignment) {
    return static_cast<T*>(allocate(sizeof(T) * count, alignment));
  }
  template <class T, class... Args>
  T* make(Args&&... args) {
    void* p = allocate(sizeof(T), alignof(T) < kDmaAlignment ? kDmaAlignment : alignof(T));
    return p ? new (p) T(static_cast<Args&&>(args)...) : nullptr;
  }

  size_t used() const { return used_; }
  size_t capacity() const { return capacity_; }
  size_t remaining() const { return capacity_ - used_; }
  size_t highWater() const { return highWater_; }
  // Rewind to a mark taken with used(). Used by FrameAllocator and scoped loads.
  void rewind(size_t mark);
  void reset() { rewind(0); }

 private:
  u8* base_ = nullptr;
  size_t capacity_ = 0;
  size_t used_ = 0;
  size_t highWater_ = 0;
  bool owns_ = false;
};

// Per-frame scratch memory: reset at the start of every frame.
class FrameAllocator {
 public:
  bool create(size_t capacity) { return arena_.create(capacity); }
  void beginFrame() { arena_.reset(); }
  void* allocate(size_t size, size_t alignment = kDmaAlignment) { return arena_.allocate(size, alignment); }
  template <class T>
  T* allocateArray(size_t count) {
    return arena_.allocateArray<T>(count);
  }
  size_t used() const { return arena_.used(); }
  size_t highWater() const { return arena_.highWater(); }
  size_t capacity() const { return arena_.capacity(); }
  Arena& arena() { return arena_; }  // for MemoryBudget registration

 private:
  Arena arena_;
};

// Fixed-block pool: `capacity` slots of T carved from an Arena once, then O(1) allocate/free
// through an intrusive free list. No heap use. Slots are raw memory until make() constructs them.
template <class T>
class PoolAllocator {
 public:
  bool init(Arena& arena, u32 capacity) {
    stride_ = alignUp(sizeof(T) > sizeof(void*) ? sizeof(T) : sizeof(void*), alignof(T) > 8 ? alignof(T) : 8);
    base_ = static_cast<u8*>(arena.allocate(stride_ * capacity, alignof(T) > kDmaAlignment ? alignof(T) : kDmaAlignment));
    if (!base_) return false;
    capacity_ = capacity;
    reset();
    return true;
  }
  // Every slot back on the free list (does not run destructors).
  void reset() {
    freeList_ = nullptr;
    for (u32 i = capacity_; i > 0; --i) {
      Node* n = reinterpret_cast<Node*>(base_ + stride_ * (i - 1));
      n->next = freeList_;
      freeList_ = n;
    }
    used_ = 0;
    peak_ = 0;
  }
  // Raw slot, or nullptr when the pool is full.
  T* allocate() {
    if (!freeList_) return nullptr;
    Node* n = freeList_;
    freeList_ = n->next;
    if (++used_ > peak_) peak_ = used_;
    return reinterpret_cast<T*>(n);
  }
  template <class... Args>
  T* make(Args&&... args) {
    void* p = allocate();
    return p ? new (p) T(static_cast<Args&&>(args)...) : nullptr;
  }
  // Returns the slot to the pool. Ignores pointers that are not slots of this pool.
  bool free(T* p) {
    u8* b = reinterpret_cast<u8*>(p);
    if (!p || b < base_ || b >= base_ + stride_ * capacity_ || (static_cast<size_t>(b - base_) % stride_) != 0) return false;
    Node* n = reinterpret_cast<Node*>(p);
    n->next = freeList_;
    freeList_ = n;
    --used_;
    return true;
  }
  void destroy(T* p) {
    if (p) {
      p->~T();
      free(p);
    }
  }
  u32 used() const { return used_; }
  u32 peak() const { return peak_; }
  u32 capacity() const { return capacity_; }

 private:
  struct Node {
    Node* next;
  };
  u8* base_ = nullptr;
  size_t stride_ = 0;
  Node* freeList_ = nullptr;
  u32 capacity_ = 0, used_ = 0, peak_ = 0;
};

// The RAM budget from the ConsoleTarget. Every long-lived arena is created through it, so an
// arena that does not fit is refused with an error that names the arena instead of crashing later.
// `reservedBytes` is memory the engine never hands out: code, stack, IOP buffers, OS slack.
struct ArenaStats {
  const char* name = nullptr;
  size_t capacity = 0, used = 0, peak = 0;
};

class MemoryBudget {
 public:
  static constexpr u32 kMaxArenas = 8;

  void init(size_t totalBytes, size_t reservedBytes);
  // Same 1/8 reserve as MemoryConfig::fromRamBudget.
  static size_t defaultReserved(size_t totalBytes) { return totalBytes / 8; }

  // Creates `arena` with `bytes` of heap memory if it fits, and registers it under `name`
  // (a string literal; kept by pointer). Returns false and logs an error naming the arena when
  // it does not fit or the table is full.
  bool createArena(Arena& arena, const char* name, size_t bytes);
  // Registers an already created arena (for example a FrameAllocator's) without charging bytes.
  bool track(const Arena& arena, const char* name);

  size_t total() const { return total_; }
  size_t reserved() const { return reserved_; }
  size_t committed() const { return committed_; }
  size_t remaining() const { return total_ - reserved_ - committed_; }
  const char* lastRefused() const { return lastRefused_; }

  u32 arenaCount() const { return count_; }
  // used/peak per arena for the overlay. False if `index` is out of range.
  bool stats(u32 index, ArenaStats& out) const;

 private:
  struct Entry {
    const char* name = nullptr;
    const Arena* arena = nullptr;
  };
  size_t total_ = 0, reserved_ = 0, committed_ = 0;
  Entry entries_[kMaxArenas];
  u32 count_ = 0;
  const char* lastRefused_ = nullptr;
};

// Sizes of the two engine allocators, derived from the ConsoleTarget RAM budget.
struct MemoryConfig {
  size_t arenaBytes = 0;
  size_t frameBytes = 0;
  // Reserve 1/8 of RAM for the OS/stack/heap slack, 1/16 of the rest for frame scratch.
  static MemoryConfig fromRamBudget(size_t ramBytes);
};

// Debug counter of operator new/delete calls (the engine replaces them).
namespace heap {
u64 allocationCount();  // total operator new calls so far
u64 totalBytes();  // total bytes requested so far

// Allocation check (contract 7). The loop calls noteFrameDone() after every frame; the count of
// operator new calls is snapshotted when the first frame finishes. Only operator new is counted
// (the engine replaces it); malloc/free called directly are not.
void noteFrameDone();
u64 checkFrames();       // frames finished since the last checkReset()
u64 checkAllocs();       // operator new calls since the end of the first frame
void checkReset();
// Logs exactly "rensga: alloc-check frames=<N> allocs=<K>" (info, channel "game"); returns K.
u64 logAllocCheck();
}  // namespace heap

// Wrap a frame: any operator new between begin and end fails the frame.
class FrameHeapGuard {
 public:
  // Start watching. Disabled guards never fail.
  static void begin();
  // Returns true if no heap allocation happened since begin(); logs an error otherwise
  // and increments violations().
  static bool end();
  static u32 violations();
  static void resetViolations();
  static void setEnabled(bool enabled);
  static bool enabled();
};

}  // namespace rensga
