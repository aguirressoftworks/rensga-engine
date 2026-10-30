#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rensga/core/backends.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
// A scratch folder per test run; removed again at the end of the test.
struct TempDir {
  char path[64];
  TempDir() {
    snprintf(path, sizeof(path), "/tmp/rensga_fs_test_%d/", static_cast<int>(rand() % 1000000));
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "mkdir -p %sdata %ssave", path, path);
    if (system(cmd) != 0) path[0] = 0;
  }
  ~TempDir() {
    if (!path[0]) return;
    char cmd[400];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    if (system(cmd) != 0) {}
  }
};
}  // namespace

TEST(fs_stream_reads_in_chunks_and_seeks) {
  TempDir t;
  CHECK(t.path[0] != 0);
  char data[100], save[100];
  snprintf(data, sizeof(data), "%sdata/", t.path);
  snprintf(save, sizeof(save), "%ssave/", t.path);
  StdioFileSystem fs(data, nullptr, save);
  u8 blob[1000];
  for (u32 i = 0; i < sizeof(blob); ++i) blob[i] = static_cast<u8>(i * 7);
  CHECK(fs.writeFile("save:/music.raw", blob, sizeof(blob)));
  // The save root is not the data root: data:/ cannot see it.
  CHECK(fs.openStream("data:/music.raw") < 0);
  // Copy it into the data folder to stream it from there.
  char cmd[800];
  snprintf(cmd, sizeof(cmd), "cp %smusic.raw %s", save, data);
  CHECK(system(cmd) == 0);

  i32 h = fs.openStream("data:/music.raw");
  CHECK(h >= 0);
  u8 chunk[256];
  u32 total = 0, reads = 0;
  for (;;) {
    u32 n = fs.readStream(h, chunk, sizeof(chunk));
    if (n == 0) break;
    CHECK(memcmp(chunk, blob + total, n) == 0);
    total += n;
    ++reads;
  }
  CHECK_EQ(total, 1000u);
  CHECK_EQ(reads, 4u);  // 256 + 256 + 256 + 232
  CHECK(fs.seekStream(h, 500));
  CHECK_EQ(fs.readStream(h, chunk, 4), 4u);
  CHECK(memcmp(chunk, blob + 500, 4) == 0);
  fs.closeStream(h);
  CHECK_EQ(fs.readStream(h, chunk, 4), 0u);  // closed handle
  CHECK(!fs.seekStream(h, 0));
}

TEST(fs_stream_slot_table_is_fixed) {
  TempDir t;
  char data[100];
  snprintf(data, sizeof(data), "%sdata/", t.path);
  StdioFileSystem fs(data);
  char cmd[400];
  snprintf(cmd, sizeof(cmd), "echo hi > %sf.txt", data);
  CHECK(system(cmd) == 0);
  i32 h[IFileSystem::kMaxStreams];
  for (i32 i = 0; i < IFileSystem::kMaxStreams; ++i) {
    h[i] = fs.openStream("data:/f.txt");
    CHECK(h[i] >= 0);
  }
  CHECK(fs.openStream("data:/f.txt") < 0);  // ninth stream: no slot
  fs.closeStream(h[3]);
  CHECK(fs.openStream("f.txt") == h[3]);  // bare path = data root, slot reused
  CHECK(fs.openStream("data:/../etc/passwd") < 0);  // cannot leave the root
}

TEST(fs_readfile_uses_virtual_data_root) {
  TempDir t;
  char data[100], cmd[400];
  snprintf(data, sizeof(data), "%sdata/", t.path);
  snprintf(cmd, sizeof(cmd), "printf abc > %sa.bin", data);
  CHECK(system(cmd) == 0);
  StdioFileSystem fs(data);
  Arena arena;
  CHECK(arena.create(4096));
  Span<const u8> s = fs.readFile("data:/a.bin", arena);
  CHECK(s && s.size == 3 && memcmp(s.data, "abc", 3) == 0);
  CHECK(!fs.readFile("data:/missing.bin", arena));
}
