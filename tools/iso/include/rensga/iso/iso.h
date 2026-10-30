// Minimal ISO 9660 (level 1 names, no Joliet/RockRidge) image writer and reader. Host tool, no dependencies.
// Output is deterministic: timestamps come from SOURCE_DATE_EPOCH (or 0), directory records and path tables are sorted.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rensga::iso {

constexpr uint32_t kSector = 2048;

struct InputFile {
  std::string isoPath;  // "SYSTEM.CNF", "DIR/NAME.EXT" (case-insensitive, stored upper case with ";1")
  std::string source;   // host file read when the image is written
};

struct Options {
  std::string volumeId = "RENSGA";
  std::vector<std::string> order;  // iso paths (or bare names) placed first on the disc, in this order
  int64_t timestamp = -1;          // unix seconds; -1 = SOURCE_DATE_EPOCH or 0
};

// Writes the image. Returns false and sets err (names the offending file) on failure.
bool writeImage(const std::string& outPath, const std::vector<InputFile>& files, const Options& opt, std::string& err);

// Reader used by tests and `rensga-iso --list`.
struct Entry {
  std::string path;  // "DIR/NAME.EXT" without ";1"
  bool isDir = false;
  uint32_t extent = 0;
  uint32_t size = 0;
};
struct Image {
  std::string volumeId;
  uint32_t totalSectors = 0;
  std::vector<Entry> entries;  // depth first, sorted
};
// Parses and cross-checks the image (PVD, both path tables, directory records, extents inside the image).
bool readImage(const std::string& path, Image& out, std::string& err);

}  // namespace rensga::iso
