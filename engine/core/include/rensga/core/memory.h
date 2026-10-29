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

 private:
  Arena arena_;
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
