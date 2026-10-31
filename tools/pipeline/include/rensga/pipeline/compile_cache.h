// F5.21: "did anything the compile depends on change?" Used by target modules to skip the compile (and the CMake configure) on a
// repeated Play. Hashes are 64-bit FNV-1a rendered as 16 hex digits; they are cache keys, not security.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rensga {
namespace pipeline {

class InputHasher {
 public:
  void addString(const std::string& s);
  // Hashes the bytes of one file (and its relative name). A missing file hashes as "absent".
  void addFileContent(const std::filesystem::path& file, const std::string& label);
  // Every file under `root` whose extension is in `exts` (".cpp", ".h", ...), sorted by path: names + contents.
  // Returns the sorted relative paths so a caller can detect added or removed files.
  std::vector<std::string> addTreeContent(const std::filesystem::path& root, const std::vector<std::string>& exts);
  // Same files but only name, size and mtime: cheap for a large tree (the engine sources).
  void addTreeStat(const std::filesystem::path& root, const std::vector<std::string>& exts);
  std::string hex() const;

 private:
  void feed(const void* p, size_t n);
  uint64_t h_ = 1469598103934665603ull;
};

// A stamp is a small text file holding one hash string.
bool stampMatches(const std::filesystem::path& stamp, const std::string& value);
void writeStamp(const std::filesystem::path& stamp, const std::string& value);

}  // namespace pipeline
}  // namespace rensga
