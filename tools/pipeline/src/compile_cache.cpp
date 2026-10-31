#include "rensga/pipeline/compile_cache.h"

#include <algorithm>
#include <fstream>

namespace rensga {
namespace pipeline {

namespace fs = std::filesystem;

void InputHasher::feed(const void* p, size_t n) {
  const unsigned char* b = static_cast<const unsigned char*>(p);
  for (size_t i = 0; i < n; ++i) {
    h_ ^= b[i];
    h_ *= 1099511628211ull;
  }
}

void InputHasher::addString(const std::string& s) {
  feed(s.data(), s.size());
  const char sep = 0;
  feed(&sep, 1);
}

void InputHasher::addFileContent(const fs::path& file, const std::string& label) {
  addString(label);
  std::ifstream f(file, std::ios::binary);
  if (!f) {
    addString("absent");
    return;
  }
  char buf[8192];
  while (f.read(buf, sizeof buf) || f.gcount() > 0) feed(buf, static_cast<size_t>(f.gcount()));
  const char sep = 1;
  feed(&sep, 1);
}

namespace {
std::vector<fs::path> listFiles(const fs::path& root, const std::vector<std::string>& exts) {
  std::vector<fs::path> out;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::string e = it->path().extension().string();
    if (std::find(exts.begin(), exts.end(), e) != exts.end()) out.push_back(it->path());
  }
  std::sort(out.begin(), out.end());
  return out;
}
}  // namespace

std::vector<std::string> InputHasher::addTreeContent(const fs::path& root, const std::vector<std::string>& exts) {
  std::vector<std::string> names;
  for (const fs::path& p : listFiles(root, exts)) {
    std::error_code ec;
    const std::string rel = fs::relative(p, root, ec).generic_string();
    names.push_back(rel);
    addFileContent(p, rel);
  }
  return names;
}

void InputHasher::addTreeStat(const fs::path& root, const std::vector<std::string>& exts) {
  for (const fs::path& p : listFiles(root, exts)) {
    std::error_code ec;
    addString(fs::relative(p, root, ec).generic_string());
    const uint64_t size = fs::file_size(p, ec);
    const auto mt = fs::last_write_time(p, ec).time_since_epoch().count();
    feed(&size, sizeof size);
    feed(&mt, sizeof mt);
  }
}

std::string InputHasher::hex() const {
  static const char* d = "0123456789abcdef";
  std::string s(16, '0');
  for (int i = 0; i < 16; ++i) s[static_cast<size_t>(15 - i)] = d[(h_ >> (4 * i)) & 15];
  return s;
}

bool stampMatches(const fs::path& stamp, const std::string& value) {
  std::ifstream f(stamp);
  std::string have;
  return f && std::getline(f, have) && have == value;
}

void writeStamp(const fs::path& stamp, const std::string& value) {
  std::error_code ec;
  fs::create_directories(stamp.parent_path(), ec);
  std::ofstream(stamp, std::ios::trunc) << value << "\n";
}

}  // namespace pipeline
}  // namespace rensga
