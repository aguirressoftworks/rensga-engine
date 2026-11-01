// F6 (A3) test helper: an in-memory IFileSystem that stores save:/ files, enough for FileSystemSaveStore. `failWrites` makes
// writeFile fail (full card); `corrupt(name)` flips a payload byte so the CRC check fails.
#pragma once
#include <string.h>

#include <map>
#include <string>
#include <vector>

#include "rensga/core/backends.h"

namespace memfs {
using namespace rensga;
class MemFs : public IFileSystem {
 public:
  std::map<std::string, std::vector<u8>> files;
  bool failWrites = false;
  u32 writes = 0;
  Span<const u8> readFile(const char*, Arena&) override { return Span<const u8>(); }
  void writeLog(const char*) override {}
  i32 openStream(const char* path) override {
    auto it = files.find(path);
    if (it == files.end()) return -1;
    for (i32 i = 0; i < kMaxStreams; ++i)
      if (!open_[i]) { open_[i] = true; cur_[i] = path; pos_[i] = 0; return i; }
    return -1;
  }
  u32 readStream(i32 h, void* dst, u32 bytes) override {
    if (h < 0 || h >= kMaxStreams || !open_[h]) return 0;
    const std::vector<u8>& f = files[cur_[h]];
    u32 n = static_cast<u32>(f.size()) - pos_[h];
    if (n > bytes) n = bytes;
    if (n) memcpy(dst, f.data() + pos_[h], n);
    pos_[h] += n;
    return n;
  }
  void closeStream(i32 h) override {
    if (h >= 0 && h < kMaxStreams) open_[h] = false;
  }
  bool writeFile(const char* path, const void* data, u32 bytes) override {
    if (failWrites) return false;
    ++writes;
    files[path].assign(static_cast<const u8*>(data), static_cast<const u8*>(data) + bytes);
    return true;
  }
  void corrupt(const char* path) {
    auto it = files.find(path);
    if (it != files.end() && it->second.size() > 20) it->second[20] ^= 0xFF;
  }

 private:
  bool open_[kMaxStreams] = {};
  std::string cur_[kMaxStreams];
  u32 pos_[kMaxStreams] = {};
};
}  // namespace memfs
