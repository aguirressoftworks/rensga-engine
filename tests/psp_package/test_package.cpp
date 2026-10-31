// F5.03-F5.05: PackageSettings (title, disc ID, version, art lookup), streamed audio list, license files. No toolchain needed.
#include <filesystem>
#include <fstream>

#include "psp_package.h"
#include "rensga_test.h"

using namespace rensga::editor;
namespace fs = std::filesystem;

namespace {
fs::path fresh(const char* name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / name;
  fs::remove_all(d);
  fs::create_directories(d / "ProjectSettings" / "Package");
  return d;
}
void put(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}
}  // namespace

TEST(package_disc_id) {
  CHECK(validDiscId("RNSG00001"));
  CHECK(validDiscId("ABCD12345"));
  CHECK(!validDiscId("rnsg00001"));
  CHECK(!validDiscId("RNSG0001"));
  CHECK(!validDiscId("RNSG000012"));
  CHECK(!validDiscId("RNS100001"));
  const std::string a = deriveDiscId("My Game"), b = deriveDiscId("My Game"), c = deriveDiscId("Other Game");
  CHECK(validDiscId(a));
  CHECK_EQ(a, b);
  CHECK(a != c);
  CHECK_EQ(a.substr(0, 4), std::string("RNSG"));
}

TEST(package_version) {
  std::string v, app;
  CHECK(normalizeVersion("1.00", v, app));
  CHECK_EQ(v, std::string("1.00"));
  CHECK_EQ(app, std::string("01.00"));
  CHECK(normalizeVersion("2.5", v, app));
  CHECK_EQ(v, std::string("2.50"));
  CHECK_EQ(app, std::string("02.50"));
  CHECK(normalizeVersion("12.34", v, app));
  CHECK_EQ(app, std::string("12.34"));
  CHECK(!normalizeVersion("1", v, app));
  CHECK(!normalizeVersion("1.", v, app));
  CHECK(!normalizeVersion("a.b", v, app));
  CHECK(!normalizeVersion("1.2.3", v, app));
}

TEST(package_title) {
  CHECK_EQ(clampSfoTitle("  Hello\tWorld  "), std::string("HelloWorld"));
  CHECK_EQ(clampSfoTitle(std::string(300, 'x')).size(), size_t(127));
  std::string u;
  for (int i = 0; i < 100; ++i) u += "\xC3\xA7";  // 200 bytes of 2-byte characters
  const std::string t = clampSfoTitle(u);
  CHECK(t.size() <= 127);
  CHECK(t.size() % 2 == 0);  // never cut inside a character
}

TEST(package_settings_defaults) {
  fs::path p = fresh("defaults");
  put(p / "ProjectSettings" / "Project.json", R"({"format":1,"name":"Space Pequi","target":"psp"})");
  std::vector<std::string> w;
  PackageSettings s = loadPackageSettings(p, &w);
  CHECK_EQ(s.title, std::string("Space Pequi"));
  CHECK_EQ(s.discId, deriveDiscId("Space Pequi"));
  CHECK_EQ(s.version, std::string("1.00"));
  CHECK_EQ(s.appVer, std::string("01.00"));
  CHECK(s.icon0.empty() && s.pic1.empty() && s.snd0.empty());
  CHECK(w.empty());
}

TEST(package_settings_overrides) {
  fs::path p = fresh("overrides");
  put(p / "ProjectSettings" / "Project.json", R"({"format":1,"name":"Space Pequi","version":"0.9"})");
  put(p / "ProjectSettings" / "Package" / "Package.json", R"({"format":1,"title":"Pequi Quest","discId":"PEQU00042","version":"1.5"})");
  put(p / "ProjectSettings" / "Package" / "ICON0.png", "x");
  put(p / "Assets" / "Package" / "PIC1.png", "x");
  put(p / "Assets" / "Package" / "SND0.AT3", "x");
  std::vector<std::string> w;
  PackageSettings s = loadPackageSettings(p, &w);
  CHECK_EQ(s.title, std::string("Pequi Quest"));
  CHECK_EQ(s.discId, std::string("PEQU00042"));
  CHECK_EQ(s.version, std::string("1.50"));
  CHECK(s.icon0.filename() == "ICON0.png" && s.icon0.parent_path().filename() == "Package");
  CHECK(!s.pic1.empty() && !s.snd0.empty());   // found under Assets/Package
  CHECK(w.empty());
}

TEST(package_settings_invalid_values_warn) {
  fs::path p = fresh("invalid");
  put(p / "ProjectSettings" / "Project.json", R"({"format":1,"name":"Bad"})");
  put(p / "ProjectSettings" / "Package" / "Package.json", R"({"discId":"bad id","version":"one"})");
  std::vector<std::string> w;
  PackageSettings s = loadPackageSettings(p, &w);
  CHECK(validDiscId(s.discId));
  CHECK_EQ(s.discId, deriveDiscId("Bad"));
  CHECK_EQ(s.version, std::string("1.00"));
  CHECK_EQ(w.size(), size_t(2));
}

TEST(package_streamed_audio) {
  fs::path p = fresh("audio");
  put(p / "Library" / "Cooked" / "psp" / "game.rpak", "pack");
  put(p / "Library" / "Cooked" / "psp" / "music.wav", "a");
  put(p / "Library" / "Cooked" / "psp" / "music2.WAV", "b");
  put(p / "Streaming" / "music.wav", "dup, the cooked one wins");
  put(p / "Streaming" / "music3.wav", "c");
  put(p / "Streaming" / "notes.txt", "no");
  auto v = streamedAudio(p, p / "Library" / "Cooked" / "psp" / "game.rpak");
  CHECK_EQ(v.size(), size_t(3));
  CHECK_EQ(v[0].filename().string(), std::string("music.wav"));
  CHECK(v[0].parent_path().filename() == "psp");
  CHECK_EQ(v[2].filename().string(), std::string("music3.wav"));
}

TEST(package_license_files) {
  fs::path p = fresh("lic");
  fs::path repo = p / "repo", mod = p / "module";
  put(repo / "LICENSE", "MIT License\n");
  put(repo / "third_party" / "stb" / "LICENSE", "stb\n");
  put(mod / "psp" / "share" / "licenses" / "pspsdk" / "LICENSE", "sdk\n");
  put(mod / "psp" / "share" / "licenses" / "sdl2" / "LICENSE.txt", "zlib\n");
  std::string err;
  auto files = writeLicenseFiles(p / "out", repo, mod, err);
  CHECK(err.empty());
  CHECK(fs::exists(p / "out" / "RENSGA-MIT.txt"));
  CHECK(fs::exists(p / "out" / "PSPSDK.txt"));
  CHECK(fs::exists(p / "out" / "SDL2-zlib.txt"));
  CHECK(fs::exists(p / "out" / "README-LICENSES.txt"));
  // without a module: the readme says so and the engine license is still there
  files = writeLicenseFiles(p / "out2", repo, {}, err);
  CHECK(fs::exists(p / "out2" / "RENSGA-MIT.txt"));
  CHECK(!fs::exists(p / "out2" / "PSPSDK.txt"));
  std::ifstream r(p / "out2" / "README-LICENSES.txt");
  std::string all((std::istreambuf_iterator<char>(r)), std::istreambuf_iterator<char>());
  CHECK(all.find("were not available") != std::string::npos);
  // no engine license: an error
  writeLicenseFiles(p / "out3", p / "nowhere", {}, err);
  CHECK(!err.empty());
}
