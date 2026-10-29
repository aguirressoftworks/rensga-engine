#include <vector>

#include "rensga/core/memory.h"
#include "rensga_test.h"

using namespace rensga;

TEST(arena_alignment_and_rewind) {
  Arena a;
  CHECK(a.create(4096));
  void* p1 = a.allocate(3);
  void* p2 = a.allocate(100, 64);
  CHECK_EQ(reinterpret_cast<uintptr_t>(p1) % 16, 0u);
  CHECK_EQ(reinterpret_cast<uintptr_t>(p2) % 64, 0u);
  size_t mark = a.used();
  a.allocate(512);
  CHECK(a.used() > mark);
  a.rewind(mark);
  CHECK_EQ(a.used(), mark);
  CHECK(a.highWater() >= mark + 512);
  CHECK(a.allocate(1 << 20) == nullptr);  // exhausted: null, no crash
}

TEST(frame_allocator_resets) {
  FrameAllocator f;
  CHECK(f.create(1024));
  void* a = f.allocate(256);
  f.beginFrame();
  void* b = f.allocate(256);
  CHECK(a == b);
  CHECK_EQ(f.used(), 256u);
}

TEST(memory_config_from_budget) {
  MemoryConfig c = MemoryConfig::fromRamBudget(25165824);
  CHECK(c.arenaBytes > 0 && c.frameBytes > 0);
  CHECK(c.arenaBytes + c.frameBytes <= 25165824u);
  CHECK_EQ(c.arenaBytes % 16, 0u);
}

TEST(frame_heap_guard_detects_allocation) {
  FrameHeapGuard::resetViolations();
  FrameHeapGuard::setEnabled(true);
  // A frame with no heap use passes.
  FrameHeapGuard::begin();
  CHECK(FrameHeapGuard::end());
  // A frame that allocates fails.
  u64 before = heap::allocationCount();
  FrameHeapGuard::begin();
  int* leak = new int(7);
  bool ok = FrameHeapGuard::end();
  delete leak;
  CHECK(!ok);
  CHECK(heap::allocationCount() > before);
  CHECK_EQ(FrameHeapGuard::violations(), 1u);
  // std::vector growth counts too.
  FrameHeapGuard::begin();
  std::vector<int> v;
  v.push_back(1);
  CHECK(!FrameHeapGuard::end());
  // Disabled guard never fails.
  FrameHeapGuard::setEnabled(false);
  FrameHeapGuard::begin();
  int* p = new int(1);
  CHECK(FrameHeapGuard::end());
  delete p;
  FrameHeapGuard::setEnabled(true);
  FrameHeapGuard::resetViolations();
}
