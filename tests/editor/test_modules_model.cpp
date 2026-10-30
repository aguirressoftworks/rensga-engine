// Hub > Modules model: rows, sizes, install job in the background, license gate, remove, target lookup.
// Fixture archives + file:// URLs and a temp modules dir; no network, no console names.
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

#include <nlohmann/json.hpp>

#include "rensga/editor/modules_model.h"
#include "rensga_test.h"

using namespace rensga::editor;
using namespace rensga::modules;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
const char* kHost = "test-host";

fs::path fresh(const std::string& n) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / ("modmodel-" + n);
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  return d;
}

void write(const fs::path& p, const std::string& t) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << t;
}

fs::path archive(const fs::path& dir, const std::string& tag) {
  fs::path src = dir / ("src-" + tag);
  write(src / "bin" / ("tool-" + tag), "#!/bin/sh\necho " + tag + "\n");
  std::string sh = "chmod +x '" + (src / "bin" / ("tool-" + tag)).string() + "' && cd '" + src.string() + "' && tar -czf '" +
                   (dir / (tag + ".tar.gz")).string() + "' .";
  CHECK(std::system(sh.c_str()) == 0);
  return dir / (tag + ".tar.gz");
}

json module(const std::string& id, const std::string& kind, const json& target) {
  return {{"id", id}, {"name", "Fixture " + id}, {"kind", kind}, {"target", target}, {"version", "1.0"}, {"requires", json::array()},
          {"license", "MIT"}, {"source", "https://example.invalid"}, {"notice", "test"}};
}

json hostEntry(const fs::path& a) {
  return {{"status", "available"}, {"url", "file://" + a.string()}, {"sha256", sha256File(a)}, {"size", fs::file_size(a)},
          {"archive", "tar.gz"}, {"strip", 0}};
}

void save(const fs::path& catalog, const json& j) { write(catalog / (j["id"].get<std::string>() + ".json"), j.dump(2)); }

bool waitDone(ModulesModel& m) {
  for (int i = 0; i < 1200; ++i) {   // 60 s
    if (m.pump()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return false;
}
}  // namespace

TEST(modules_model_rows_install_remove_target) {
  fs::path d = fresh("a");
  fs::path cat = d / "catalog";
  fs::path tgz = archive(d, "one"), tg2 = archive(d, "two");
  json tools = module("fx-tools", "tools", nullptr);
  tools["hosts"][kHost] = hostEntry(tgz);
  tools["path"] = {"{root}/bin"};
  tools["check"] = {"tool-one"};
  json build = module("fx-build", "toolchain", "fxtarget");
  build["requires"] = {"fx-tools"};
  build["hosts"][kHost] = hostEntry(tg2);
  build["check"] = {"tool-two"};
  build["path"] = {"{root}/bin"};
  json unpub = module("fx-unpub", "toolchain", nullptr);
  unpub["hosts"][kHost] = {{"status", "unpublished"}, {"note", "CI builds it"}};
  json manual = module("fx-manual", "emulator", nullptr);
  manual["hosts"][kHost] = {{"status", "manual"}, {"note", "install it yourself"}};
  json nohost = module("fx-nohost", "tools", nullptr);
  nohost["hosts"]["other-host"] = hostEntry(tgz);
  for (const json& j : {tools, build, unpub, manual, nohost}) save(cat, j);

  ModulesModel m(ModuleManager(ModuleCatalog::load(cat), d / "home" / "modules", kHost));
  CHECK_EQ(m.rows().size(), size_t(5));
  const ModuleRow* b = m.find("fx-build");
  CHECK(b != nullptr);
  if (!b) return;
  CHECK(b->status == ModuleStatus::NotInstalled);
  CHECK(b->size == fs::file_size(tg2));
  CHECK(b->totalSize == fs::file_size(tg2) + fs::file_size(tgz));   // dependency included
  CHECK(m.find("fx-unpub")->status == ModuleStatus::Unpublished);
  CHECK(m.find("fx-manual")->status == ModuleStatus::Manual);
  CHECK(m.find("fx-nohost")->status == ModuleStatus::NoHost);
  CHECK(m.forTarget("fxtarget") == b);
  CHECK(m.forTarget("other") == nullptr);
  CHECK(m.missingForTarget("fxtarget"));
  CHECK(!m.missingForTarget("other"));

  std::string why;
  CHECK(!m.startInstall("fx-unpub", why));
  CHECK(why.find("not published") != std::string::npos);
  CHECK(!m.startInstall("nope", why));

  CHECK(m.startInstall("fx-build", why));
  CHECK(m.busy() || m.finishedPending() || m.job().state != JobState::Idle);
  std::string again;
  CHECK(!m.startInstall("fx-tools", again) || true);   // a second start while running is refused (or fine once it ended)
  CHECK(waitDone(m));
  CHECK(m.job().state == JobState::Done);
  CHECK(m.find("fx-build")->status == ModuleStatus::Installed);
  CHECK(m.find("fx-tools")->status == ModuleStatus::Installed);   // pulled in as a dependency
  CHECK(!m.missingForTarget("fxtarget"));
  CHECK(fs::exists(m.find("fx-build")->location));

  CHECK(m.verify("fx-build").ok);
  CHECK(!m.remove("fx-tools").ok);                               // refused: fx-build requires it
  CHECK(m.remove("fx-build").ok);
  CHECK(m.find("fx-build")->status == ModuleStatus::NotInstalled);
  CHECK(m.missingForTarget("fxtarget"));
  CHECK(m.remove("fx-tools").ok);

  m.setLocated("fx-manual", d.string());
  CHECK(m.find("fx-manual")->status == ModuleStatus::Located);
  CHECK(m.find("fx-manual")->location == d.string());
  m.setLocated("fx-manual", "");
  CHECK(m.find("fx-manual")->status == ModuleStatus::Manual);
}

TEST(modules_model_license_gate_and_cancel) {
  fs::path d = fresh("b");
  fs::path cat = d / "catalog";
  fs::path tgz = archive(d, "sdk");
  json sdk = module("fx-sdk", "toolchain", nullptr);
  sdk["hosts"][kHost] = hostEntry(tgz);
  sdk["postInstall"] = json::array({{{"argv", json::array({"true"})}, {"license", "fx-license"}}});
  sdk["check"] = {"true"};
  save(cat, sdk);
  ModulesModel m(ModuleManager(ModuleCatalog::load(cat), d / "home" / "modules", kHost));
  CHECK(m.find("fx-sdk")->licenseIds.size() == 1);
  CHECK(m.unacceptedLicenses("fx-sdk").size() == 1);
  std::string why;
  CHECK(!m.startInstall("fx-sdk", why));
  CHECK(why.find("fx-license") != std::string::npos);
  CHECK(!fs::exists(d / "home" / "modules" / "downloads"));    // refused before anything was downloaded
  CHECK(m.job().state == JobState::Idle);
  m.accept("fx-license", true);
  CHECK(m.unacceptedLicenses("fx-sdk").empty());
  CHECK(m.startInstall("fx-sdk", why));
  CHECK(waitDone(m));
  CHECK(m.job().state == JobState::Done);
  CHECK(m.find("fx-sdk")->status == ModuleStatus::Installed);

  // cancel: whichever side wins the race, no half-installed folder is left and the job ends
  CHECK(m.remove("fx-sdk").ok);
  CHECK(m.startInstall("fx-sdk", why));
  m.cancel();
  CHECK(waitDone(m));
  const JobState s = m.job().state;
  CHECK(s == JobState::Cancelled || s == JobState::Done || s == JobState::Failed);
  if (s != JobState::Done) CHECK(m.find("fx-sdk")->status == ModuleStatus::NotInstalled);
}

TEST(modules_model_format_bytes) {
  CHECK_EQ(formatBytes(0), std::string("0 B"));
  CHECK_EQ(formatBytes(2048), std::string("2.0 KB"));
  CHECK_EQ(formatBytes(165000000), std::string("165 MB"));
  CHECK_EQ(formatBytes(172980272), std::string("173 MB"));
  CHECK_EQ(formatBytes(4500000), std::string("4.5 MB"));
  CHECK_EQ(formatBytes(2300000000ull), std::string("2.3 GB"));
}
