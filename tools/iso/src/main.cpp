// rensga-iso: builds a deterministic ISO 9660 image (replaces xorriso for PS2 disc images).
//   rensga-iso -o out.iso -V VOLID [--order SYSTEM.CNF,MAIN.ELF,...] <dir or files...>
//   rensga-iso --list image.iso
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include "rensga/iso/iso.h"

namespace fs = std::filesystem;
using namespace rensga::iso;


int main(int argc, char** argv) {
  std::string out, err;
  Options opt;
  std::vector<InputFile> files;
  std::vector<std::string> inputs;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--list" && i + 1 < argc) {
      Image img;
      if (!readImage(argv[i + 1], img, err)) { fprintf(stderr, "rensga-iso: %s\n", err.c_str()); return 1; }
      printf("volume %s, %u sectors\n", img.volumeId.c_str(), img.totalSectors);
      for (const Entry& e : img.entries) printf("%s%s extent=%u size=%u\n", e.path.c_str(), e.isDir ? "/" : "", e.extent, e.size);
      return 0;
    }
    if (a == "-o" && i + 1 < argc) out = argv[++i];
    else if (a == "-V" && i + 1 < argc) opt.volumeId = argv[++i];
    else if (a == "--order" && i + 1 < argc) {
      std::string l = argv[++i];
      for (size_t s = 0; s <= l.size();) {
        size_t e = l.find(',', s);
        if (e == std::string::npos) e = l.size();
        if (e > s) opt.order.push_back(l.substr(s, e - s));
        s = e + 1;
      }
    } else if (!a.empty() && a[0] == '-') { fprintf(stderr, "rensga-iso: unknown option %s\n", a.c_str()); return 2; }
    else inputs.push_back(a);
  }
  if (out.empty() || inputs.empty()) {
    fprintf(stderr, "usage: rensga-iso -o out.iso -V VOLID [--order A,B,...] <dir or files...>\n       rensga-iso --list image.iso\n");
    return 2;
  }
  std::error_code ec;
  for (const std::string& in : inputs) {
    if (fs::is_directory(in, ec)) {
      for (auto it = fs::recursive_directory_iterator(in, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file()) continue;
        files.push_back({fs::relative(it->path(), in).generic_string(), it->path().string()});
      }
    } else {
      files.push_back({fs::path(in).filename().string(), in});
    }
  }
  if (!writeImage(out, files, opt, err)) { fprintf(stderr, "rensga-iso: %s\n", err.c_str()); return 1; }
  printf("rensga-iso: wrote %s (%zu files)\n", out.c_str(), files.size());
  return 0;
}
