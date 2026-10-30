#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

#include "rensga/iso/iso.h"
#include "rensga_test.h"

using namespace rensga::iso;
namespace fs = std::filesystem;

namespace {
std::string tmpDir() {
  fs::path p = fs::temp_directory_path() / ("rensga_iso_test_" + std::to_string((unsigned long)rand()));
  fs::create_directories(p);
  return p.string();
}
std::string put(const std::string& dir, const std::string& name, size_t size, char fill) {
  std::string p = dir + "/" + name;
  std::ofstream(p, std::ios::binary) << std::string(size, fill);
  return p;
}
std::string slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}
}  // namespace

TEST(iso_layout_parses_back) {
  std::string d = tmpDir();
  std::string cnf = "BOOT2 = cdrom0:\\RENSGA.ELF;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n";
  std::ofstream(d + "/SYSTEM.CNF", std::ios::binary) << cnf;
  std::vector<InputFile> in = {{"game.rpk", put(d, "g", 5000, 'G')}, {"RENSGA.ELF", put(d, "e", 70001, 'E')},
                               {"SYSTEM.CNF", d + "/SYSTEM.CNF"}, {"music.wav", put(d, "m", 2048, 'M')},
                               {"data/sub/a.bin", put(d, "a", 3, 'A')}, {"data/empty.txt", put(d, "z", 0, 'Z')}};
  Options opt;
  opt.order = {"SYSTEM.CNF", "RENSGA.ELF", "GAME.RPK", "MUSIC.WAV"};
  std::string err;
  CHECK(writeImage(d + "/a.iso", in, opt, err));
  Image img;
  CHECK(readImage(d + "/a.iso", img, err));
  if (!err.empty()) printf("err: %s\n", err.c_str());
  CHECK(img.volumeId == "RENSGA");
  std::map<std::string, Entry> m;
  for (auto& e : img.entries) m[e.path] = e;
  CHECK(m.size() == 8);
  CHECK(m["SYSTEM.CNF"].size == cnf.size());
  CHECK(m["RENSGA.ELF"].size == 70001);
  CHECK(m["GAME.RPK"].size == 5000);
  CHECK(m["MUSIC.WAV"].size == 2048);
  CHECK(m["DATA"].isDir && m["DATA/SUB"].isDir);
  CHECK(m["DATA/SUB/A.BIN"].size == 3);
  // Order: SYSTEM.CNF first, then the ELF, the pack and the music, contiguous.
  CHECK(m["SYSTEM.CNF"].extent < m["RENSGA.ELF"].extent);
  CHECK(m["RENSGA.ELF"].extent + 35 == m["GAME.RPK"].extent);  // 70001 bytes = 35 sectors
  CHECK(m["GAME.RPK"].extent + 3 == m["MUSIC.WAV"].extent);
  // File contents are at their extents.
  std::string img_bytes = slurp(d + "/a.iso");
  CHECK(img_bytes.size() == (size_t)img.totalSectors * kSector);
  CHECK(img_bytes.compare((size_t)m["SYSTEM.CNF"].extent * kSector, cnf.size(), cnf) == 0);
  CHECK(img_bytes[(size_t)m["RENSGA.ELF"].extent * kSector + 70000] == 'E');
  CHECK(img_bytes[(size_t)m["RENSGA.ELF"].extent * kSector + 70001] == 0);
  CHECK(img_bytes[(size_t)m["DATA/SUB/A.BIN"].extent * kSector + 2] == 'A');
  fs::remove_all(d);
}

TEST(iso_is_deterministic) {
  std::string d = tmpDir(), err;
  std::vector<InputFile> in = {{"B.BIN", put(d, "b", 4097, 'B')}, {"A.BIN", put(d, "a", 10, 'A')}};
  Options opt;
  opt.volumeId = "X";
  CHECK(writeImage(d + "/1.iso", in, opt, err));
  std::reverse(in.begin(), in.end());
  CHECK(writeImage(d + "/2.iso", in, opt, err));
  CHECK(slurp(d + "/1.iso") == slurp(d + "/2.iso"));
  opt.timestamp = 86400LL * 365;
  CHECK(writeImage(d + "/3.iso", in, opt, err));
  CHECK(slurp(d + "/1.iso") != slurp(d + "/3.iso"));
  fs::remove_all(d);
}

TEST(iso_many_entries_span_directory_sectors) {
  std::string d = tmpDir(), err;
  std::vector<InputFile> in;
  std::string f = put(d, "f", 1, 'F');
  for (int i = 0; i < 200; ++i) in.push_back({"F" + std::to_string(i) + ".DAT", f});
  CHECK(writeImage(d + "/a.iso", in, Options{}, err));
  Image img;
  CHECK(readImage(d + "/a.iso", img, err));
  CHECK(img.entries.size() == 200);
  fs::remove_all(d);
}

TEST(iso_rejects_bad_names) {
  std::string d = tmpDir(), err;
  std::string f = put(d, "f", 1, 'F');
  CHECK(!writeImage(d + "/a.iso", {{"toolongname.bin", f}}, Options{}, err));
  CHECK(err.find("toolongname.bin") != std::string::npos);
  CHECK(!writeImage(d + "/a.iso", {{"A.BIN", f}, {"a.bin", f}}, Options{}, err));
  CHECK(!writeImage(d + "/a.iso", {{"A.BIN", d + "/missing"}}, Options{}, err));
  Options o;
  o.order = {"NOPE.BIN"};
  CHECK(!writeImage(d + "/a.iso", {{"A.BIN", f}}, o, err));
  fs::remove_all(d);
}
