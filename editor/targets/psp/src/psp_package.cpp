#include "psp_package.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

namespace {

nlohmann::json readJson(const fs::path& p) {
  try {
    std::ifstream f(p);
    if (!f) return nlohmann::json::object();
    nlohmann::json j = nlohmann::json::parse(f);
    return j.is_object() ? j : nlohmann::json::object();
  } catch (...) {
    return nlohmann::json::object();
  }
}

std::string str(const nlohmann::json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

fs::path firstExisting(const fs::path& project, const char* name) {
  std::error_code ec;
  for (const char* dir : {"ProjectSettings/Package", "Assets/Package"}) {
    fs::path p = project / dir / name;
    if (fs::is_regular_file(p, ec)) return p;
  }
  return {};
}

bool copyText(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  if (!fs::is_regular_file(from, ec)) return false;
  fs::create_directories(to.parent_path(), ec);
  fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
  return !ec;
}

bool writeText(const fs::path& to, const std::string& text) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  std::ofstream f(to, std::ios::binary);
  f << text;
  return static_cast<bool>(f);
}

}  // namespace

std::string deriveDiscId(const std::string& name) {
  uint32_t h = 2166136261u;  // FNV-1a
  for (unsigned char c : name) { h ^= c; h *= 16777619u; }
  char buf[16];
  std::snprintf(buf, sizeof buf, "RNSG%05u", static_cast<unsigned>(h % 100000u));
  return buf;
}

bool validDiscId(const std::string& id) {
  if (id.size() != 9) return false;
  for (size_t i = 0; i < 9; ++i) {
    const unsigned char c = static_cast<unsigned char>(id[i]);
    if (i < 4 ? !(c >= 'A' && c <= 'Z') : !(c >= '0' && c <= '9')) return false;
  }
  return true;
}

bool normalizeVersion(const std::string& in, std::string& version, std::string& appVer) {
  size_t dot = in.find('.');
  if (dot == std::string::npos || dot == 0 || dot + 1 >= in.size()) return false;
  std::string maj = in.substr(0, dot), min = in.substr(dot + 1);
  auto digits = [](const std::string& s) { return !s.empty() && s.size() <= 2 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; }); };
  if (!digits(maj) || !digits(min)) return false;
  if (min.size() == 1) min += "0";
  if (maj.size() == 1) maj = "0" + maj;
  appVer = maj + "." + min;
  version = (maj[0] == '0' ? maj.substr(1) : maj) + "." + min;
  return true;
}

std::string clampSfoTitle(const std::string& title) {
  std::string t;
  for (unsigned char c : title)
    if (c >= 0x20 && c != 0x7f) t.push_back(static_cast<char>(c));
  while (!t.empty() && t.back() == ' ') t.pop_back();
  size_t b = t.find_first_not_of(' ');
  t = b == std::string::npos ? std::string() : t.substr(b);
  if (t.size() > 127) {
    size_t n = 127;
    while (n > 0 && (static_cast<unsigned char>(t[n]) & 0xC0) == 0x80) --n;  // do not cut inside a UTF-8 sequence
    t.resize(n);
  }
  return t;
}

PackageSettings loadPackageSettings(const fs::path& project, std::vector<std::string>* warnings) {
  auto warn = [&](const std::string& w) { if (warnings) warnings->push_back(w); };
  const nlohmann::json proj = readJson(project / "ProjectSettings" / "Project.json");
  const nlohmann::json pkg = readJson(project / "ProjectSettings" / "Package" / "Package.json");
  PackageSettings s;
  const std::string name = str(proj, "name").empty() ? std::string("Rensga Game") : str(proj, "name");
  s.title = clampSfoTitle(str(pkg, "title").empty() ? name : str(pkg, "title"));
  if (s.title.empty()) s.title = "Rensga Game";
  s.discId = str(pkg, "discId");
  if (s.discId.empty()) {
    s.discId = deriveDiscId(name);
  } else if (!validDiscId(s.discId)) {
    warn("package: discId '" + s.discId + "' is not four capital letters and five digits (like RNSG00001); using " + deriveDiscId(name));
    s.discId = deriveDiscId(name);
  }
  std::string v = str(pkg, "version");
  if (v.empty()) v = str(proj, "version");
  if (v.empty() || !normalizeVersion(v, s.version, s.appVer)) {
    if (!v.empty()) warn("package: version '" + v + "' is not <major>.<minor> (like 1.00); using 1.00");
    normalizeVersion("1.00", s.version, s.appVer);
  }
  s.icon0 = firstExisting(project, "ICON0.png");
  s.pic1 = firstExisting(project, "PIC1.png");
  s.snd0 = firstExisting(project, "SND0.AT3");
  return s;
}

std::vector<fs::path> streamedAudio(const fs::path& project, const fs::path& cookedPack) {
  std::vector<fs::path> out;
  std::vector<std::string> seen;
  std::error_code ec;
  for (const fs::path& dir : {cookedPack.parent_path(), project / "Streaming"}) {
    std::vector<fs::path> here;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
      std::string ext = it->path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (it->is_regular_file(ec) && ext == ".wav") here.push_back(it->path());
    }
    std::sort(here.begin(), here.end());
    for (const fs::path& p : here) {
      const std::string n = p.filename().string();
      if (std::find(seen.begin(), seen.end(), n) != seen.end()) continue;
      seen.push_back(n);
      out.push_back(p);
    }
  }
  return out;
}

std::vector<fs::path> writeLicenseFiles(const fs::path& dest, const fs::path& repoRoot, const fs::path& moduleRoot, std::string& error) {
  std::vector<fs::path> out;
  std::error_code ec;
  fs::remove_all(dest, ec);
  fs::create_directories(dest, ec);
  auto add = [&](const fs::path& from, const std::string& as) {
    if (copyText(from, dest / as)) out.push_back(dest / as);
    return fs::exists(dest / as, ec);
  };
  if (!add(repoRoot / "LICENSE", "RENSGA-MIT.txt")) { error = "missing " + (repoRoot / "LICENSE").string(); return out; }
  add(repoRoot / "third_party" / "stb" / "LICENSE", "stb.txt");
  bool sdk = false, sdl = false, newlib = false;
  if (!moduleRoot.empty()) {
    const fs::path lic = moduleRoot / "psp" / "share" / "licenses";
    sdk = add(lic / "pspsdk" / "LICENSE", "PSPSDK.txt");
    sdl = add(lic / "sdl2" / "LICENSE.txt", "SDL2-zlib.txt");
    newlib = false;
    for (const char* n : {"COPYING", "COPYING.NEWLIB", "LICENSE"}) if (add(lic / "newlib" / n, "newlib.txt")) { newlib = true; break; }
  }
  std::ostringstream r;
  r << "Licenses of the software inside this game's EBOOT.PBP\n"
       "=====================================================\n\n"
       "RENSGA-MIT.txt   Rensga Engine runtime and API (MIT).\n"
       "stb.txt          stb_image / stb_image_write (MIT or public domain).\n";
  if (sdk) r << "PSPSDK.txt       PSPSDK (pspdev), the PSP system libraries the game links.\n";
  if (sdl) r << "SDL2-zlib.txt    SDL2 (zlib license), used for the pad and audio.\n";
  if (newlib) r << "newlib.txt       newlib, the C library.\n";
  if (!sdk || !sdl)
    r << "\nThe SDK license files were not available when this package was made (build with the PSP Build Support module installed).\n"
         "PSPSDK: https://github.com/pspdev/pspsdk (BSD-style licenses)\n"
         "SDL2:   https://libsdl.org (zlib license), the PSP port is part of pspdev.\n";
  r << "\nThe emulators Rensga starts to test a game (PPSSPP and others) are separate programs and are not part of this package.\n"
       "Rensga Engine is open source (MIT); keep this folder with the game.\n";
  writeText(dest / "README-LICENSES.txt", r.str());
  out.push_back(dest / "README-LICENSES.txt");
  return out;
}

}  // namespace editor
}  // namespace rensga
