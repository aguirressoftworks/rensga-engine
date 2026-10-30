#include <string.h>

#include "rensga/core/backends.h"
#include "rensga/core/backends.h"
#include "rensga/core/log.h"
#include "rensga/core/memory.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
struct Node {
  u32 a, b;
  explicit Node(u32 v = 0) : a(v), b(v * 2) {}
};
char g_lastError[512];
void captureError(const LogRecord& r, void*) {
  if (r.level == LogLevel::Error) {
    strncpy(g_lastError, r.message, sizeof(g_lastError) - 1);
    g_lastError[sizeof(g_lastError) - 1] = 0;
  }
}
}  // namespace

TEST(pool_alloc_free_reuse) {
  Arena a;
  CHECK(a.create(4096));
  PoolAllocator<Node> pool;
  CHECK(pool.init(a, 4));
  Node* n[4];
  for (u32 i = 0; i < 4; ++i) {
    n[i] = pool.make(i + 1);
    CHECK(n[i] != nullptr);
    CHECK_EQ(n[i]->b, (i + 1) * 2);
  }
  CHECK_EQ(pool.used(), 4u);
  CHECK(pool.allocate() == nullptr);  // full: null, no crash
  CHECK(pool.free(n[1]));
  CHECK_EQ(pool.used(), 3u);
  Node* again = pool.make(9);
  CHECK(again == n[1]);  // the freed slot is reused
  CHECK_EQ(pool.peak(), 4u);
  Node outside;
  CHECK(!pool.free(&outside));  // not a slot of this pool
  CHECK_EQ(pool.used(), 4u);
  pool.reset();
  CHECK_EQ(pool.used(), 0u);
  CHECK(pool.allocate() != nullptr);
}

TEST(pool_does_not_use_the_heap) {
  Arena a;
  CHECK(a.create(4096));
  PoolAllocator<Node> pool;
  CHECK(pool.init(a, 8));
  u64 before = heap::allocationCount();
  for (int round = 0; round < 3; ++round) {
    Node* n[8];
    for (int i = 0; i < 8; ++i) n[i] = pool.make(i);
    for (int i = 0; i < 8; ++i) pool.destroy(n[i]);
  }
  CHECK_EQ(heap::allocationCount(), before);
  CHECK_EQ(pool.used(), 0u);
}

TEST(budget_refuses_over_budget_arena_and_names_it) {
  CallbackSink sink(captureError, nullptr);
  log::addSink(&sink);
  g_lastError[0] = 0;
  MemoryBudget b;
  b.init(1 << 20, 1 << 18);  // 1 MB total, 256 KB reserved -> 768 KB usable
  Arena world, textures;
  CHECK(b.createArena(world, "world", 1 << 19));
  CHECK_EQ(b.committed(), static_cast<size_t>(1 << 19));
  CHECK(!b.createArena(textures, "texture-cache", 1 << 19));  // only 256 KB left
  CHECK(textures.capacity() == 0);
  CHECK(strstr(g_lastError, "texture-cache") != nullptr);
  CHECK(strcmp(b.lastRefused(), "texture-cache") == 0);
  CHECK_EQ(b.committed(), static_cast<size_t>(1 << 19));  // a refusal charges nothing
  CHECK(b.createArena(textures, "texture-cache", 1 << 18));
  CHECK_EQ(b.remaining(), 0u);
  log::removeSink(&sink);
}

TEST(budget_stats_report_used_and_peak) {
  MemoryBudget b;
  b.init(MemoryConfig::fromRamBudget(1 << 20).arenaBytes + (1 << 16), MemoryBudget::defaultReserved(1 << 20));
  Arena a;
  FrameAllocator f;
  CHECK(b.createArena(a, "world", 1 << 14));
  CHECK(f.create(1 << 12));
  CHECK(b.track(f.arena(), "frame"));
  CHECK_EQ(b.arenaCount(), 2u);
  a.allocate(1000);
  f.allocate(200);
  f.beginFrame();  // used falls back to 0, peak stays
  ArenaStats s;
  CHECK(b.stats(0, s));
  CHECK(strcmp(s.name, "world") == 0 && s.used >= 1000 && s.peak == s.used);
  CHECK(b.stats(1, s));
  CHECK(strcmp(s.name, "frame") == 0 && s.used == 0 && s.peak >= 200);
  CHECK(!b.stats(2, s));
}

TEST(ring_sink_keeps_last_records) {
  RingSink ring;
  for (int i = 0; i < 70; ++i) {
    char m[32];
    snprintf(m, sizeof(m), "msg %d", i);
    LogRecord r{LogLevel::Warn, "test", m};
    ring.write(r);
  }
  CHECK_EQ(ring.count(), RingSink::kCapacity);
  CHECK(strcmp(ring.entry(0)->message, "msg 6") == 0);
  CHECK(strcmp(ring.entry(ring.count() - 1)->message, "msg 69") == 0);
  CHECK(ring.entry(ring.count()) == nullptr);
}

namespace {
struct LogCapture : NullFileSystem {
  char last[256] = {};
  void writeLog(const char* t) override { strncpy(last, t, sizeof(last) - 1); }
};
}  // namespace

TEST(fs_log_sink_adds_rensga_prefix_once) {
  LogCapture fs;
  FsLogSink sink(&fs);
  sink.write(LogRecord{LogLevel::Error, "core", "boom"});
  CHECK(strcmp(fs.last, "rensga: ERROR [core] boom") == 0);
  sink.write(LogRecord{LogLevel::Info, "game", "rensga: frame 60"});
  CHECK(strcmp(fs.last, "rensga: frame 60") == 0);
}
