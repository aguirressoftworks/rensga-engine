// Installer, catalog, environment and resolver checks. Everything is local: fixture archives + file:// URLs.
#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

#include <sys/stat.h>

#include "rensga/modules/modules.h"
#include "rensga/pipeline/toolchain.h"
#include "rensga_test.h"

using namespace rensga::modules;
using rensga::pipeline::ToolchainBackend;
using rensga::pipeline::ToolchainMode;
using rensga::pipeline::ToolchainRunner;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const char* kHost = "test-host";

fs::path freshDir(const std::string& name) {
  fs::path d = fs::path(RENSGA_TEST_TMP_DIR) / name;
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  return d;
}

void writeFile(const fs::path& p, const std::string& text, bool exec = false) {
  fs::create_directories(p.parent_path());
  std::ofstream(p) << text;
  if (exec) chmod(p.c_str(), 0755);
}

std::string slurp(const fs::path& p) {
  std::ifstream in(p);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

int run(const std::string& cmd) { return std::system(cmd.c_str()); }

// pkg/<top>/bin/hello (a script that prints hello-<tag>) as .tar.gz or .zip, via `cmake -E tar` (no zip/tar dependency).
fs::path makeArchive(const fs::path& dir, const std::string& name, const std::string& top, const std::string& tag, bool zip) {
  fs::path src = dir / ("src-" + name);
  fs::path base = top.empty() ? src : src / top;
  writeFile(base / "bin" / ("hello-" + tag), "#!/bin/sh\necho hello-" + tag + "\n", true);
  writeFile(base / "share" / (tag + ".txt"), tag + "\n");
  fs::path out = dir / (name + (zip ? ".zip" : ".tar.gz"));
  std::string cmd = zip ? "cd '" + src.string() + "' && '" + RENSGA_TEST_CMAKE + "' -E tar cf '" + out.string() + "' --format=zip -- ."
                        : "cd '" + src.string() + "' && tar -czf '" + out.string() + "' .";
  CHECK(run(cmd) == 0);
  return out;
}

json hostJson(const fs::path& archive, const std::string& type, int strip, const std::string& sha = "", const std::string& dest = "") {
  json p = {{"url", "file://" + archive.string()}, {"sha256", sha.empty() ? sha256File(archive) : sha}, {"size", fs::file_size(archive)},
            {"archive", type}, {"strip", strip}};
  if (!dest.empty()) p["dest"] = dest;
  return p;
}

void writeManifest(const fs::path& catalogDir, const json& j) { writeFile(catalogDir / (j["id"].get<std::string>() + ".json"), j.dump(2)); }

json baseModule(const std::string& id, const std::string& version = "1.0") {
  return {{"id", id}, {"name", "Fixture " + id}, {"kind", "toolchain"}, {"target", nullptr}, {"version", version}, {"requires", json::array()},
          {"license", "MIT"}, {"source", "https://example.invalid"}, {"notice", "test"}};
}

ModuleManager manager(const fs::path& catalogDir, const fs::path& home, const std::string& host = kHost) {
  return ModuleManager(ModuleCatalog::load(catalogDir), home / "modules", host);
}

size_t countTmp(const fs::path& modulesDir) {
  size_t n = 0;
  std::error_code ec;
  for (fs::directory_iterator it(modulesDir, ec), end; !ec && it != end; it.increment(ec))
    if (it->path().filename().string().rfind(".tmp-", 0) == 0 || it->path().filename().string().rfind(".old-", 0) == 0) ++n;
  return n;
}

}  // namespace

TEST(modules_sha256_vectors) {
  CHECK_EQ(sha256Bytes(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
  CHECK_EQ(sha256Bytes("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
  CHECK_EQ(sha256Bytes(std::string(1000, 'a')), std::string("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"));
  fs::path d = freshDir("sha");
  writeFile(d / "f", "abc");
  CHECK_EQ(sha256File(d / "f"), sha256Bytes("abc"));
  CHECK(sha256File(d / "missing").empty());
}

TEST(modules_catalog_loading) {
  fs::path d = freshDir("catalog");
  json m = baseModule("fx-a");
  m["hosts"] = {{"h1", {{"status", "unpublished"}}}, {"h2", {{"url", "file:///x.tar.gz"}, {"sha256", std::string(64, 'a')}, {"strip", 1}}}};
  m["env"] = {{"FX", "{root}/x"}};
  m["path"] = {"{root}/bin"};
  writeManifest(d, m);
  writeFile(d / "wrong-name.json", baseModule("other").dump());
  writeFile(d / "broken.json", "{ not json");
  ModuleCatalog c = ModuleCatalog::load(d);
  CHECK_EQ(c.all().size(), size_t(1));
  CHECK_EQ(c.errors().size(), size_t(2));
  const Module* a = c.find("fx-a");
  CHECK(a != nullptr);
  if (!a) return;
  CHECK_EQ(a->hosts.at("h2").parts[0].archive, std::string("tar.gz"));  // guessed from the url
  CHECK_EQ(a->hosts.at("h2").parts[0].strip, 1);
  CHECK(a->hosts.at("h1").parts.empty());
  CHECK(c.find("nope") == nullptr);
}

TEST(modules_install_targz_zip_parts_env_verify_remove) {
  fs::path d = freshDir("install");
  fs::path tgz = makeArchive(d, "tool", "pkg", "one", false);
  fs::path zip = makeArchive(d, "extra", "", "two", true);
  json m = baseModule("fx-tool");
  m["hosts"][kHost] = {{"status", "available"}, {"parts", json::array({hostJson(tgz, "tar.gz", 1), hostJson(zip, "zip", 0, "", "bin2")})}};
  m["env"] = {{"FX_HOME", "{root}"}};
  m["path"] = {"{root}/bin"};
  m["check"] = {"hello-one"};
  writeManifest(d / "catalog", m);
  ModuleManager mm = manager(d / "catalog", d / "home");
  CHECK(!mm.isInstalled("fx-tool"));
  std::vector<std::string> phases;
  Result r = mm.install("fx-tool", [&](const Progress& p) { phases.push_back(p.phase); });
  if (!r.ok) fprintf(stderr, "  install failed: %s\n", r.message.c_str());
  CHECK(r.ok);
  CHECK(mm.isInstalled("fx-tool"));
  const fs::path root = mm.root("fx-tool");
  CHECK(fs::exists(root / "bin" / "hello-one"));
  CHECK(fs::exists(root / "share" / "one.txt"));
  CHECK(fs::exists(root / "bin2" / "bin" / "hello-two"));  // second part landed in its dest
  CHECK(!phases.empty() && phases.back() == "done");
  CHECK(countTmp(d / "home" / "modules") == 0);
  CHECK(fs::exists(d / "home" / "modules" / "installed.json"));
  CHECK(mm.verify("fx-tool").ok);
  CHECK_EQ(mm.installed().size(), size_t(1));

  ModuleEnv env;
  CHECK(mm.envFor("fx-tool", env));
  CHECK_EQ(env.vars.size(), size_t(1));
  CHECK_EQ(env.vars[0].second, root.string());
  CHECK_EQ(env.pathDirs[0], (root / "bin").string());
  auto flat = env.flatten("/usr/bin");
  CHECK_EQ(flat.back().first, std::string("PATH"));
  CHECK_EQ(flat.back().second, (root / "bin").string() + ":/usr/bin");

  // a second manager instance (new process in real life) sees the same install
  ModuleManager again = manager(d / "catalog", d / "home");
  CHECK(again.isInstalled("fx-tool"));
  // reinstall of an installed module is a no-op, remove deletes it
  CHECK(again.install("fx-tool").ok);
  Result rm = again.remove("fx-tool");
  CHECK(rm.ok);
  CHECK(!fs::exists(root));
  CHECK(!again.isInstalled("fx-tool"));
  CHECK(!again.remove("fx-tool").ok);
  // and it can be installed again from the download cache
  CHECK(again.install("fx-tool").ok);
  CHECK(again.isInstalled("fx-tool"));
}

TEST(modules_sha_mismatch_is_rejected) {
  fs::path d = freshDir("badsha");
  fs::path tgz = makeArchive(d, "tool", "pkg", "one", false);
  json m = baseModule("fx-bad");
  m["hosts"][kHost] = hostJson(tgz, "tar.gz", 1, std::string(64, '0'));
  m["check"] = {"true"};
  writeManifest(d / "catalog", m);
  ModuleManager mm = manager(d / "catalog", d / "home");
  Result r = mm.install("fx-bad");
  CHECK(!r.ok);
  CHECK(r.message.find("sha256 mismatch") != std::string::npos);
  CHECK(!mm.isInstalled("fx-bad"));
  CHECK(!fs::exists(mm.root("fx-bad")));
  CHECK(countTmp(d / "home" / "modules") == 0);
  // the bad download is discarded, not cached
  size_t files = 0;
  for (auto& e : fs::directory_iterator(d / "home" / "modules" / "downloads")) { (void)e; ++files; }
  CHECK_EQ(files, size_t(0));
}

TEST(modules_missing_sha_is_refused) {
  fs::path d = freshDir("nosha");
  fs::path tgz = makeArchive(d, "tool", "pkg", "one", false);
  json m = baseModule("fx-nosha");
  m["hosts"][kHost] = hostJson(tgz, "tar.gz", 1);
  m["hosts"][kHost]["sha256"] = "";
  writeManifest(d / "catalog", m);
  ModuleManager mm = manager(d / "catalog", d / "home");
  Result r = mm.install("fx-nosha");
  CHECK(!r.ok);
  CHECK(r.message.find("sha256") != std::string::npos);
}

TEST(modules_cancel_leaves_no_partial_dir) {
  fs::path d = freshDir("cancel");
  fs::path tgz = makeArchive(d, "tool", "pkg", "one", false);
  json m = baseModule("fx-cancel");
  m["hosts"][kHost] = hostJson(tgz, "tar.gz", 1);
  m["check"] = {"true"};
  writeManifest(d / "catalog", m);
  ModuleManager mm = manager(d / "catalog", d / "home");
  // 1) cancelled before it starts
  std::atomic<bool> stop{true};
  Result r = mm.install("fx-cancel", {}, &stop);
  CHECK(!r.ok);
  CHECK(r.message.find("cancelled") != std::string::npos);
  CHECK(!fs::exists(mm.root("fx-cancel")));
  // 2) cancelled while unpacking (the flag flips on the extract progress event)
  std::atomic<bool> stop2{false};
  r = mm.install("fx-cancel", [&](const Progress& p) { if (p.phase == "extract") stop2 = true; }, &stop2);
  CHECK(!r.ok);
  CHECK(!fs::exists(mm.root("fx-cancel")));
  CHECK(!mm.isInstalled("fx-cancel"));
  CHECK(countTmp(d / "home" / "modules") == 0);
  // 3) afterwards a normal install works
  CHECK(mm.install("fx-cancel").ok);
}

TEST(modules_dependencies_and_env_order) {
  fs::path d = freshDir("deps");
  fs::path a = makeArchive(d, "a", "pkg", "a", false);
  fs::path b = makeArchive(d, "b", "pkg", "b", false);
  json ma = baseModule("fx-base");
  ma["hosts"][kHost] = hostJson(a, "tar.gz", 1);
  ma["env"] = {{"BASE_HOME", "{root}"}};
  ma["path"] = {"{root}/bin"};
  ma["check"] = {"hello-a"};
  json mb = baseModule("fx-top");
  mb["requires"] = {"fx-base"};
  mb["hosts"][kHost] = hostJson(b, "tar.gz", 1);
  mb["env"] = {{"TOP_BASE", "{root:fx-base}/share"}, {"TOP_HOME", "{root}"}};
  mb["path"] = {"{root}/bin"};
  mb["check"] = {"hello-b"};  // hello-a must also be reachable? no: only b is checked, but base env is on PATH
  writeManifest(d / "catalog", ma);
  writeManifest(d / "catalog", mb);
  ModuleManager mm = manager(d / "catalog", d / "home");
  ModuleEnv env;
  std::string err;
  CHECK(!mm.envFor("fx-top", env, &err));  // nothing installed yet
  CHECK(err.find("rensga modules install") != std::string::npos);
  Result r = mm.install("fx-top");
  if (!r.ok) fprintf(stderr, "  %s\n", r.message.c_str());
  CHECK(r.ok);
  CHECK(mm.isInstalled("fx-base"));  // dependency installed first
  CHECK(mm.isInstalled("fx-top"));
  env = ModuleEnv();
  CHECK(mm.envFor("fx-top", env));
  CHECK_EQ(env.vars.size(), size_t(3));
  CHECK_EQ(env.vars[0].first, std::string("BASE_HOME"));  // dependencies first
  CHECK_EQ(env.vars[1].second, (mm.root("fx-base") / "share").string());
  CHECK_EQ(env.pathDirs.size(), size_t(2));
  CHECK_EQ(env.pathDirs[0], (mm.root("fx-top") / "bin").string());  // own directories win
  CHECK_EQ(env.pathDirs[1], (mm.root("fx-base") / "bin").string());
  // a dependency cannot be removed while something installed needs it
  Result rm = mm.remove("fx-base");
  CHECK(!rm.ok);
  CHECK(rm.message.find("fx-top") != std::string::npos);
  CHECK(mm.remove("fx-top").ok);
  CHECK(mm.remove("fx-base").ok);
}

TEST(modules_license_gated_post_install) {
  fs::path d = freshDir("license");
  fs::path a = makeArchive(d, "a", "pkg", "a", false);
  json m = baseModule("fx-lic");
  m["hosts"][kHost] = hostJson(a, "tar.gz", 1);
  m["postInstall"] = json::array({{{"argv", {"sh", "-c", "echo done > \"$0/post.txt\"", "{root}"}}, {"license", "fx-eula"}}});
  writeManifest(d / "catalog", m);
  ModuleManager mm = manager(d / "catalog", d / "home");
  // refused: nothing downloaded, nothing installed
  Result r = mm.install("fx-lic");
  CHECK(!r.ok);
  CHECK(r.message.find("fx-eula") != std::string::npos);
  CHECK(r.message.find("accept") != std::string::npos);
  CHECK(!fs::exists(d / "home" / "modules" / "downloads"));
  CHECK(!fs::exists(mm.root("fx-lic")));
  r = mm.install("fx-lic", {}, nullptr, [](const std::string&, const Module&) { return false; });
  CHECK(!r.ok);
  CHECK(!mm.isInstalled("fx-lic"));
  // accepted: the step runs
  std::string asked;
  r = mm.install("fx-lic", {}, nullptr, [&](const std::string& lic, const Module&) { asked = lic; return true; });
  if (!r.ok) fprintf(stderr, "  %s\n", r.message.c_str());
  CHECK(r.ok);
  CHECK_EQ(asked, std::string("fx-eula"));
  CHECK_EQ(slurp(mm.root("fx-lic") / "post.txt"), std::string("done\n"));
}

TEST(modules_failed_check_or_post_install_is_rolled_back) {
  fs::path d = freshDir("rollback");
  fs::path a = makeArchive(d, "a", "pkg", "a", false);
  json m1 = baseModule("fx-badcheck");
  m1["hosts"][kHost] = hostJson(a, "tar.gz", 1);
  m1["check"] = {"false"};
  json m2 = baseModule("fx-badpost");
  m2["hosts"][kHost] = hostJson(a, "tar.gz", 1);
  m2["postInstall"] = json::array({{{"argv", {"false"}}}});
  writeManifest(d / "catalog", m1);
  writeManifest(d / "catalog", m2);
  ModuleManager mm = manager(d / "catalog", d / "home");
  Result r = mm.install("fx-badcheck");
  CHECK(!r.ok);
  CHECK(r.message.find("check") != std::string::npos);
  CHECK(!fs::exists(mm.root("fx-badcheck")));
  CHECK(!mm.isInstalled("fx-badcheck"));
  r = mm.install("fx-badpost");
  CHECK(!r.ok);
  CHECK(!fs::exists(mm.root("fx-badpost")));
  CHECK(!mm.isInstalled("fx-badpost"));
}

TEST(modules_unknown_unpublished_and_manual_hosts_say_so) {
  fs::path d = freshDir("hosts");
  fs::path a = makeArchive(d, "a", "pkg", "a", false);
  json m = baseModule("fx-hosts");
  m["hosts"]["only-here"] = hostJson(a, "tar.gz", 1);
  m["hosts"]["later-host"] = {{"status", "unpublished"}, {"note", "no upstream build"}};
  m["hosts"]["hand-host"] = {{"status", "manual"}, {"note", "install it yourself"}};
  writeManifest(d / "catalog", m);
  std::string why;
  {
    ModuleManager mm = manager(d / "catalog", d / "home", "mystery-host");
    CHECK(mm.availability("fx-hosts", &why) == Availability::UnknownHost);
    CHECK(why.find("no build for mystery-host") != std::string::npos);
    CHECK(why.find("only-here") != std::string::npos);
    Result r = mm.install("fx-hosts");
    CHECK(!r.ok);
    CHECK(r.message.find("mystery-host") != std::string::npos);
    CHECK(mm.availability("nope", &why) == Availability::UnknownModule);
    CHECK(!mm.install("nope").ok);
  }
  {
    ModuleManager mm = manager(d / "catalog", d / "home", "later-host");
    CHECK(mm.availability("fx-hosts", &why) == Availability::Unpublished);
    Result r = mm.install("fx-hosts");
    CHECK(!r.ok);
    CHECK(r.message.find("not published for later-host yet") != std::string::npos);
    CHECK(r.message.find("no upstream build") != std::string::npos);
  }
  {
    ModuleManager mm = manager(d / "catalog", d / "home", "hand-host");
    CHECK(mm.availability("fx-hosts", &why) == Availability::Manual);
    CHECK(!mm.install("fx-hosts").ok);
    CHECK(why.find("install it yourself") != std::string::npos);
  }
}

TEST(modules_home_override_and_host_id) {
  CHECK(!ModuleManager::hostId().empty());
  setenv("RENSGA_HOME", "/tmp/fx-rensga-home", 1);
  CHECK_EQ(ModuleManager::defaultModulesDir(), fs::path("/tmp/fx-rensga-home/modules"));
  unsetenv("RENSGA_HOME");
  CHECK(ModuleManager::defaultModulesDir().string().find("rensga") != std::string::npos);
}

TEST(toolchain_resolver_prefers_native_then_docker_then_error) {
  fs::path d = freshDir("resolver");
  fs::path a = makeArchive(d, "a", "pkg", "a", false);
  json m = baseModule("fx-tc");
  m["name"] = "Fixture Build Support";
  m["hosts"][kHost] = hostJson(a, "tar.gz", 1);
  m["env"] = {{"FX_TC", "{root}"}};
  m["path"] = {"{root}/bin"};
  m["check"] = {"hello-a"};
  writeManifest(d / "catalog", m);
  fs::path badDocker = d / "docker-bad.sh", goodDocker = d / "docker-good.sh";
  writeFile(badDocker, "#!/bin/sh\nexit 1\n", true);
  writeFile(goodDocker, "#!/bin/sh\n[ \"$1\" = true ] && exit 0\nexec \"$@\"\n", true);
  auto runner = [&](const fs::path& wrapper, ToolchainMode mode) {
    return ToolchainRunner(manager(d / "catalog", d / "home"), "fx-tc", wrapper, mode);
  };
  std::string err;

  // nothing installed, docker unusable: the install hint, in every mode that allows it
  {
    ToolchainRunner tr = runner(badDocker, ToolchainMode::Auto);
    CHECK(!tr.resolve(err));
    CHECK_EQ(err, std::string("Fixture Build Support is not installed. Install it from Hub > Modules or run: rensga modules install fx-tc"));
    ToolchainRunner none = runner({}, ToolchainMode::Auto);
    CHECK(!none.resolve(err));
    CHECK(err.find("rensga modules install fx-tc") != std::string::npos);
    ToolchainRunner nat = runner(goodDocker, ToolchainMode::Native);
    CHECK(!nat.resolve(err));  // native only: usable docker does not help
    CHECK(err.find("rensga modules install fx-tc") != std::string::npos);
    ToolchainRunner dk = runner(badDocker, ToolchainMode::Docker);
    CHECK(!dk.resolve(err));
    CHECK(err.find("Docker is not usable") != std::string::npos);
  }
  // nothing installed, docker works: contributor fallback in auto and docker mode
  {
    ToolchainRunner tr = runner(goodDocker, ToolchainMode::Auto);
    CHECK(tr.resolve(err));
    CHECK(tr.backend() == ToolchainBackend::Docker);
    auto ps = tr.spec({"echo", "hi"}, d, {{"K", "V"}});
    CHECK_EQ(ps.argv[0], goodDocker.string());
    CHECK_EQ(ps.argv[1], std::string("echo"));
    CHECK_EQ(ps.env.size(), size_t(1));
  }
  // installed: native wins, even when docker works; its env is on the process spec
  ModuleManager mm = manager(d / "catalog", d / "home");
  CHECK(mm.install("fx-tc").ok);
  {
    ToolchainRunner tr = runner(goodDocker, ToolchainMode::Auto);
    CHECK(tr.resolve(err));
    CHECK(tr.backend() == ToolchainBackend::Native);
    auto ps = tr.spec({"hello-a"}, d, {{"K", "V"}});
    CHECK_EQ(ps.argv[0], std::string("hello-a"));
    bool path = false, home = false, k = false;
    for (auto& kv : ps.env) {
      path = path || (kv.first == "PATH" && kv.second.find((mm.root("fx-tc") / "bin").string()) == 0);
      home = home || (kv.first == "FX_TC" && kv.second == mm.root("fx-tc").string());
      k = k || kv.first == "K";
    }
    CHECK(path);
    CHECK(home);
    CHECK(k);
    std::string out;
    int rc = rensga::pipeline::runProcess(ps, [&](const std::string& l) { out += l; });
    CHECK_EQ(rc, 0);
    CHECK_EQ(out, std::string("hello-a"));
    ToolchainRunner dk = runner(goodDocker, ToolchainMode::Docker);
    CHECK(dk.resolve(err));
    CHECK(dk.backend() == ToolchainBackend::Docker);  // an explicit docker request is honoured
  }
}

TEST(toolchain_resolver_explains_unpublished_host) {
  fs::path d = freshDir("resolver-host");
  json m = baseModule("fx-later");
  m["name"] = "Later Build Support";
  m["hosts"][kHost] = {{"status", "unpublished"}};
  writeManifest(d / "catalog", m);
  ToolchainRunner tr(manager(d / "catalog", d / "home"), "fx-later", {}, ToolchainMode::Auto);
  std::string err;
  CHECK(!tr.resolve(err));
  CHECK(err.find("not published for test-host yet") != std::string::npos);
  ToolchainRunner unknown(manager(d / "catalog", d / "home"), "fx-missing", {}, ToolchainMode::Auto);
  CHECK(!unknown.resolve(err));
  CHECK(err.find("unknown module") != std::string::npos || err.find("not installed") != std::string::npos);
}
