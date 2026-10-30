// Build support modules: catalog, installer, environment. See tools/modules/README.md.
#include "rensga/modules/modules.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

#include <unistd.h>

#include "rensga/pipeline/process.h"

namespace rensga {
namespace modules {

namespace fs = std::filesystem;
using nlohmann::json;

#if defined(_WIN32)
static const char kPathSep = ';';
#else
static const char kPathSep = ':';
#endif

// ---------------------------------------------------------------- catalog

namespace {

std::string str(const json& j, const char* key) {
  auto it = j.find(key);
  return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::vector<std::string> strList(const json& j, const char* key) {
  std::vector<std::string> v;
  auto it = j.find(key);
  if (it != j.end() && it->is_array())
    for (const auto& e : *it)
      if (e.is_string()) v.push_back(e.get<std::string>());
  return v;
}

std::string guessArchive(const std::string& url) {
  auto ends = [&](const char* s) {
    std::string e = s;
    return url.size() >= e.size() && url.compare(url.size() - e.size(), e.size(), e) == 0;
  };
  if (ends(".tar.gz") || ends(".tgz")) return "tar.gz";
  if (ends(".tar.xz")) return "tar.xz";
  if (ends(".zip")) return "zip";
  return "";
}

ArchivePart parsePart(const json& j) {
  ArchivePart p;
  p.url = str(j, "url");
  p.sha256 = str(j, "sha256");
  p.archive = str(j, "archive");
  if (p.archive.empty()) p.archive = guessArchive(p.url);
  p.dest = str(j, "dest");
  p.size = j.value("size", uint64_t(0));
  p.strip = j.value("strip", 0);
  return p;
}

Module parseModule(const json& j) {
  Module m;
  m.id = str(j, "id");
  m.name = str(j, "name");
  m.kind = str(j, "kind");
  if (j.contains("target") && j["target"].is_string()) m.target = j["target"].get<std::string>();
  m.version = str(j, "version");
  m.requires_ = strList(j, "requires");
  m.license = str(j, "license");
  m.source = str(j, "source");
  m.notice = str(j, "notice");
  if (j.contains("hosts") && j["hosts"].is_object()) {
    for (auto it = j["hosts"].begin(); it != j["hosts"].end(); ++it) {
      HostEntry h;
      h.status = str(it.value(), "status");
      if (h.status.empty()) h.status = "available";
      h.note = str(it.value(), "note");
      if (it.value().contains("parts") && it.value()["parts"].is_array()) {
        for (const auto& pj : it.value()["parts"]) {
          h.parts.push_back(parsePart(pj));
        }
      } else if (!str(it.value(), "url").empty()) {
        h.parts.push_back(parsePart(it.value()));
      }
      m.hosts[it.key()] = std::move(h);
    }
  }
  if (j.contains("env") && j["env"].is_object())
    for (auto it = j["env"].begin(); it != j["env"].end(); ++it)
      if (it.value().is_string()) m.env.emplace_back(it.key(), it.value().get<std::string>());
  m.path = strList(j, "path");
  if (j.contains("postInstall") && j["postInstall"].is_array())
    for (const auto& s : j["postInstall"]) {
      PostInstallStep st;
      st.argv = strList(s, "argv");
      st.license = str(s, "license");
      m.postInstall.push_back(std::move(st));
    }
  m.check = strList(j, "check");
  return m;
}

fs::path findInPath(const std::string& name) {
  const char* p = std::getenv("PATH");
  if (!p) return {};
  std::stringstream ss(p);
  std::string dir;
  while (std::getline(ss, dir, kPathSep)) {
    if (dir.empty()) continue;
    for (const char* ext : {"", ".exe"}) {
      fs::path f = fs::path(dir) / (name + ext);
      std::error_code ec;
      if (fs::is_regular_file(f, ec) && access(f.c_str(), X_OK) == 0) return f;
    }
  }
  return {};
}

}  // namespace

void ModuleCatalog::add(Module m) { modules_.push_back(std::move(m)); }

ModuleCatalog ModuleCatalog::load(const fs::path& dir) {
  ModuleCatalog c;
  std::error_code ec;
  std::vector<fs::path> files;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (it->path().extension() == ".json") files.push_back(it->path());
  if (files.empty()) c.errors_.push_back("no module manifests in " + dir.string());
  std::sort(files.begin(), files.end());
  for (const fs::path& f : files) {
    try {
      std::ifstream in(f);
      json j = json::parse(in);
      Module m = parseModule(j);
      if (m.id.empty() || m.version.empty()) {
        c.errors_.push_back(f.filename().string() + ": id and version are required");
        continue;
      }
      if (f.stem().string() != m.id) {
        c.errors_.push_back(f.filename().string() + ": file name must be <id>.json (id is '" + m.id + "')");
        continue;
      }
      c.modules_.push_back(std::move(m));
    } catch (const std::exception& e) {
      c.errors_.push_back(f.filename().string() + ": " + e.what());
    }
  }
  return c;
}

fs::path ModuleCatalog::defaultDir() {
  if (const char* e = std::getenv("RENSGA_MODULE_CATALOG")) return e;
  if (const char* r = std::getenv("RENSGA_ROOT")) {
    fs::path p = fs::path(r) / "tools" / "modules" / "manifest.d";
    std::error_code ec;
    if (fs::is_directory(p, ec)) return p;
  }
  return fs::path(RENSGA_MODULES_SOURCE_DIR) / "manifest.d";
}

const Module* ModuleCatalog::find(const std::string& id) const {
  for (const Module& m : modules_)
    if (m.id == id) return &m;
  return nullptr;
}

std::vector<std::pair<std::string, std::string>> ModuleEnv::flatten(const std::string& inheritedPath) const {
  auto out = vars;
  if (!pathDirs.empty()) {
    std::string p;
    for (const std::string& d : pathDirs) {
      if (!p.empty()) p += kPathSep;
      p += d;
    }
    if (!inheritedPath.empty()) p += std::string(1, kPathSep) + inheritedPath;
    out.emplace_back("PATH", p);
  }
  return out;
}

// ---------------------------------------------------------------- manager basics

std::string ModuleManager::hostId() {
#if defined(_WIN32)
  const char* os = "windows";
#elif defined(__APPLE__)
  const char* os = "macos";
#else
  const char* os = "linux";
#endif
#if defined(__x86_64__) || defined(_M_X64)
  const char* arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  const char* arch = "aarch64";
#else
  const char* arch = "unknown";
#endif
  return std::string(os) + "-" + arch;
}

fs::path ModuleManager::defaultModulesDir() {
  if (const char* h = std::getenv("RENSGA_HOME"); h && *h) return fs::path(h) / "modules";
#if defined(_WIN32)
  if (const char* a = std::getenv("LOCALAPPDATA"); a && *a) return fs::path(a) / "Rensga" / "modules";
  return fs::path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : ".") / "AppData" / "Local" / "Rensga" / "modules";
#else
  if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) return fs::path(x) / "rensga" / "modules";
  const char* home = std::getenv("HOME");
  return fs::path(home ? home : ".") / ".local" / "share" / "rensga" / "modules";
#endif
}

ModuleManager::ModuleManager(ModuleCatalog catalog, fs::path modulesDir, std::string host)
    : catalog_(std::move(catalog)),
      dir_(modulesDir.empty() ? defaultModulesDir() : std::move(modulesDir)),
      host_(host.empty() ? hostId() : std::move(host)) {}

std::vector<InstalledRecord> ModuleManager::installed() const {
  std::vector<InstalledRecord> out;
  try {
    std::ifstream in(dir_ / "installed.json");
    if (!in) return out;
    json j = json::parse(in);
    for (const auto& e : j.value("modules", json::array()))
      out.push_back({str(e, "id"), str(e, "version"), str(e, "host"), str(e, "root")});
  } catch (...) {
    out.clear();
  }
  return out;
}

void ModuleManager::writeInstalled(const std::vector<InstalledRecord>& recs) const {
  json j;
  j["modules"] = json::array();
  for (const auto& r : recs) j["modules"].push_back({{"id", r.id}, {"version", r.version}, {"host", r.host}, {"root", r.root}});
  std::error_code ec;
  fs::create_directories(dir_, ec);
  fs::path tmp = dir_ / ("installed.json.tmp" + std::to_string(getpid()));
  {
    std::ofstream out(tmp, std::ios::trunc);
    out << j.dump(2) << "\n";
  }
  fs::rename(tmp, dir_ / "installed.json", ec);
}

fs::path ModuleManager::root(const std::string& id) const {
  const Module* m = catalog_.find(id);
  return m ? dir_ / (m->id + "-" + m->version) : fs::path();
}

bool ModuleManager::isInstalled(const std::string& id) const {
  const Module* m = catalog_.find(id);
  if (!m) return false;
  for (const auto& r : installed()) {
    std::error_code ec;
    if (r.id == id && r.version == m->version && fs::is_directory(root(id), ec)) return true;
  }
  return false;
}

Availability ModuleManager::availability(const std::string& id, std::string* why) const {
  auto say = [&](const std::string& s) { if (why) *why = s; };
  const Module* m = catalog_.find(id);
  if (!m) {
    say("unknown module '" + id + "'. Run: rensga modules list");
    return Availability::UnknownModule;
  }
  auto it = m->hosts.find(host_);
  if (it == m->hosts.end()) {
    std::string have;
    for (const auto& kv : m->hosts) have += (have.empty() ? "" : ", ") + kv.first;
    say(m->name + " has no build for " + host_ + (have.empty() ? "" : " (it exists for: " + have + ")") + ".");
    return Availability::UnknownHost;
  }
  const HostEntry& h = it->second;
  if (h.status == "manual") {
    say(m->name + " is not downloadable on " + host_ + ": install it yourself" + (h.note.empty() ? "" : " (" + h.note + ")") +
        " and point the editor at it in Preferences.");
    return Availability::Manual;
  }
  if (h.status == "unpublished" || h.parts.empty()) {
    say(m->name + " is not published for " + host_ + " yet. Our CI builds it (.github/workflows/toolchains.yml); it appears in Hub > Modules once released" +
        (h.note.empty() ? "." : ". " + h.note));
    return Availability::Unpublished;
  }
  return Availability::Available;
}

std::string ModuleManager::expand(const std::string& text, const Module& m) const {
  std::string out;
  size_t i = 0;
  while (i < text.size()) {
    if (text[i] == '{') {
      size_t e = text.find('}', i);
      if (e != std::string::npos) {
        std::string key = text.substr(i + 1, e - i - 1);
        if (key == "root") { out += root(m.id).string(); i = e + 1; continue; }
        if (key.rfind("root:", 0) == 0) { out += root(key.substr(5)).string(); i = e + 1; continue; }
      }
    }
    out += text[i++];
  }
  return out;
}

bool ModuleManager::envFor(const std::string& id, ModuleEnv& out, std::string* error) const {
  std::set<std::string> seen;
  std::function<bool(const std::string&)> walk = [&](const std::string& mid) {
    if (seen.count(mid)) return true;
    seen.insert(mid);
    const Module* m = catalog_.find(mid);
    if (!m) { if (error) *error = "unknown module '" + mid + "'"; return false; }
    if (!isInstalled(mid)) {
      if (error) *error = m->name + " is not installed. Install it from Hub > Modules or run: rensga modules install " + mid;
      return false;
    }
    for (const auto& r : m->requires_)
      if (!walk(r)) return false;
    for (const auto& kv : m->env) out.vars.emplace_back(kv.first, expand(kv.second, *m));
    // The module's own directories go in front of its dependencies' (and so of everything already collected).
    std::vector<std::string> own;
    for (const auto& p : m->path) own.push_back(expand(p, *m));
    out.pathDirs.insert(out.pathDirs.begin(), own.begin(), own.end());
    return true;
  };
  return walk(id);
}

std::vector<HostToolProbe> ModuleManager::probeHostTools() const {
  std::vector<HostToolProbe> v;
  for (const char* n : {"curl", "tar", "bsdtar", "unzip", "cmake", "ninja", "docker"}) {
    fs::path p = findInPath(n);
    v.push_back({n, !p.empty(), p.string()});
  }
  return v;
}

// ---------------------------------------------------------------- subprocess helpers

namespace {

constexpr int kCancelled = -2;

// Runs argv to completion, calling tick() about every 100 ms. Returns the exit code, kCancelled, or -1 (cannot start).
int runTool(const std::vector<std::string>& argv, const std::vector<std::pair<std::string, std::string>>& env,
            const fs::path& cwd, std::string& output, const std::atomic<bool>* cancel, const std::function<void()>& tick) {
  pipeline::ProcessSpec ps;
  ps.argv = argv;
  ps.env = env;
  ps.cwd = cwd;
  pipeline::Process p;
  std::mutex mu;
  std::string out;
  if (!p.start(ps, [&](const std::string& l) {
        std::lock_guard<std::mutex> lock(mu);
        if (out.size() < 8192) out += l + "\n";
      }))
    return -1;
  while (p.running()) {
    if (cancel && cancel->load()) { p.terminate(); return kCancelled; }
    if (tick) tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  int rc = p.wait();
  std::lock_guard<std::mutex> lock(mu);
  output = out;
  return rc;
}

std::string lastLine(const std::string& s) {
  std::string t = s;
  while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
  size_t n = t.find_last_of('\n');
  return n == std::string::npos ? t : t.substr(n + 1);
}

uint64_t fileSize(const fs::path& p) {
  std::error_code ec;
  auto n = fs::file_size(p, ec);
  return ec ? 0 : uint64_t(n);
}

std::string baseName(const std::string& url) {
  std::string u = url.substr(0, url.find_first_of("?#"));
  size_t s = u.find_last_of('/');
  return s == std::string::npos ? u : u.substr(s + 1);
}

// Moves src into dst; merges directories that exist on both sides.
bool moveMerge(const fs::path& src, const fs::path& dst, std::string& err) {
  std::error_code ec;
  if (!fs::exists(fs::symlink_status(dst, ec))) {
    fs::rename(src, dst, ec);
    if (ec) { err = "cannot move " + src.string() + ": " + ec.message(); return false; }
    return true;
  }
  if (fs::is_directory(src) && fs::is_directory(dst) && !fs::is_symlink(src) && !fs::is_symlink(dst)) {
    std::vector<fs::path> kids;
    for (fs::directory_iterator it(src, ec), end; !ec && it != end; it.increment(ec)) kids.push_back(it->path());
    for (const auto& k : kids)
      if (!moveMerge(k, dst / k.filename(), err)) return false;
    return true;
  }
  err = "two archives of one module both contain " + dst.string();
  return false;
}

}  // namespace

// ---------------------------------------------------------------- install

bool ModuleManager::runCheck(const Module& m, std::string& output) const {
  if (m.check.empty()) { output = "no check command"; return true; }
  ModuleEnv env;
  std::string err;
  // During install the module is not recorded yet: collect its dependencies and its own entries by hand.
  std::set<std::string> seen;
  std::function<void(const Module&)> walk = [&](const Module& mm) {
    if (seen.count(mm.id)) return;
    seen.insert(mm.id);
    for (const auto& r : mm.requires_)
      if (const Module* d = catalog_.find(r)) walk(*d);
    for (const auto& kv : mm.env) env.vars.emplace_back(kv.first, expand(kv.second, mm));
    std::vector<std::string> own;
    for (const auto& p : mm.path) own.push_back(expand(p, mm));
    env.pathDirs.insert(env.pathDirs.begin(), own.begin(), own.end());
  };
  walk(m);
  std::vector<std::string> argv;
  for (const auto& a : m.check) argv.push_back(expand(a, m));
  const char* inherited = std::getenv("PATH");
  std::string out;
  int rc = runTool(argv, env.flatten(inherited ? inherited : ""), {}, out, nullptr, {});
  output = out.empty() ? ("exit " + std::to_string(rc)) : out.substr(0, out.find('\n'));
  return rc == 0;
}

Result ModuleManager::install(const std::string& id, ProgressFn progress, const std::atomic<bool>* cancel, LicenseFn acceptLicense) {
  // 1. resolve the dependency chain (dependencies first) and make sure every step is possible before touching the disk.
  std::vector<const Module*> chain;
  std::set<std::string> seen;
  std::string problem;
  std::function<bool(const std::string&)> walk = [&](const std::string& mid) {
    if (seen.count(mid)) return true;
    seen.insert(mid);
    const Module* m = catalog_.find(mid);
    if (!m) { problem = "unknown module '" + mid + "'. Run: rensga modules list"; return false; }
    for (const auto& r : m->requires_)
      if (!walk(r)) return false;
    chain.push_back(m);
    return true;
  };
  if (!walk(id)) return {false, problem};
  for (const Module* m : chain) {
    if (isInstalled(m->id)) continue;
    std::string why;
    if (availability(m->id, &why) != Availability::Available) return {false, why};
    for (const auto& step : m->postInstall) {
      if (step.license.empty()) continue;
      if (!acceptLicense || !acceptLicense(step.license, *m))
        return {false, "License '" + step.license + "' must be accepted before installing " + m->name +
                           ". Read it, then accept it in Hub > Modules or pass --accept-licenses to rensga modules install."};
    }
  }
  // 2. install
  for (const Module* m : chain) {
    if (isInstalled(m->id)) continue;
    Result r;
    try {
      r = installOne(*m, progress, cancel, acceptLicense);
    } catch (const std::exception& e) {
      r = {false, std::string("install of ") + m->name + " failed: " + e.what()};
    }
    if (!r.ok) return r;
  }
  if (progress) progress({id, "done", 0, 0, ""});
  return {true, catalog_.find(id)->name + " is installed at " + root(id).string()};
}

Result ModuleManager::installOne(const Module& m, const ProgressFn& progress, const std::atomic<bool>* cancel, const LicenseFn&) {
  auto report = [&](const char* phase, uint64_t done, uint64_t total, const std::string& detail) {
    if (progress) progress({m.id, phase, done, total, detail});
  };
  auto cancelled = [&]() { return cancel && cancel->load(); };
  auto fail = [&](const std::string& msg) { return Result{false, msg}; };
  const HostEntry& host = m.hosts.at(host_);
  std::error_code ec;
  fs::create_directories(dir_ / "downloads", ec);
  if (ec) return fail("cannot create " + (dir_ / "downloads").string() + ": " + ec.message());
  if (cancelled()) return fail("install of " + m.name + " was cancelled");

  // download + verify every part (cached by file name; a cached file is trusted only when its sha256 matches)
  std::vector<fs::path> archives;
  for (size_t i = 0; i < host.parts.size(); ++i) {
    const ArchivePart& p = host.parts[i];
    const std::string tag = host.parts.size() > 1 ? " (part " + std::to_string(i + 1) + "/" + std::to_string(host.parts.size()) + ")" : "";
    if (p.sha256.size() != 64) return fail(m.id + " manifest has no valid sha256 for " + p.url + "; refusing to install an unverified archive");
    if (p.archive.empty()) return fail(m.id + ": unknown archive type for " + p.url);
    fs::path cache = dir_ / "downloads" / (m.id + "-" + m.version + "-" + baseName(p.url));
    fs::path part = cache;
    part += ".part";
    report("verify", 0, 0, "checking cache" + tag);
    if (fs::exists(cache, ec) && sha256File(cache) != p.sha256) fs::remove(cache, ec);
    if (!fs::exists(cache, ec)) {
      bool resumed = true;
      for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt == 1) { fs::remove(part, ec); resumed = false; }
        if (p.size && fileSize(part) > p.size) fs::remove(part, ec);
        if (p.size && fileSize(part) == p.size) break;  // a complete .part from an earlier run
        report("download", fileSize(part), p.size, baseName(p.url) + tag);
        std::vector<std::string> argv = {"curl", "-fL", "-sS", "--retry", "3", "--connect-timeout", "30"};
        if (resumed && fileSize(part) > 0) { argv.push_back("-C"); argv.push_back("-"); }
        argv.insert(argv.end(), {"-o", part.string(), p.url});
        std::string out;
        int rc = runTool(argv, {}, {}, out, cancel, [&]() { report("download", fileSize(part), p.size, baseName(p.url) + tag); });
        if (rc == kCancelled) return fail("install of " + m.name + " was cancelled (the partial download is kept and resumes next time)");
        if (rc == -1) return fail("curl is not available; install curl (Windows 10+ ships curl.exe) or download " + p.url + " by hand");
        if (rc == 0) break;
        if (attempt == 1) return fail("download of " + p.url + " failed: " + lastLine(out));
      }
      report("verify", 0, 0, "sha256" + tag);
      std::string got = sha256File(part);
      if (got != p.sha256) {
        fs::remove(part, ec);
        return fail("sha256 mismatch for " + baseName(p.url) + ": expected " + p.sha256 + " but got " + got + ". The download was discarded.");
      }
      fs::rename(part, cache, ec);
      if (ec) return fail("cannot move the download into place: " + ec.message());
    }
    archives.push_back(cache);
    if (cancelled()) return fail("install of " + m.name + " was cancelled");
  }

  // unpack into a temp dir next to the final root (same filesystem, so the last step is one rename)
  const std::string stamp = std::to_string(getpid());
  const fs::path tmp = dir_ / (".tmp-" + m.id + "-" + stamp);
  const fs::path stage = tmp / "root";
  auto cleanup = [&]() { std::error_code e; fs::remove_all(tmp, e); };
  cleanup();
  fs::create_directories(stage, ec);
  if (ec) return fail("cannot create " + stage.string() + ": " + ec.message());
  for (size_t i = 0; i < host.parts.size(); ++i) {
    const ArchivePart& p = host.parts[i];
    fs::path x = tmp / ("x" + std::to_string(i));
    fs::create_directories(x, ec);
    report("extract", 0, 0, baseName(p.url));
    std::vector<std::string> argv;
    fs::path cwd;
    if (p.archive == "zip") {
#if defined(_WIN32)
      argv = {"tar", "-xf", archives[i].string(), "-C", x.string()};
#else
      if (!findInPath("bsdtar").empty()) argv = {"bsdtar", "-xf", archives[i].string(), "-C", x.string()};
      else if (!findInPath("unzip").empty()) argv = {"unzip", "-q", "-o", archives[i].string(), "-d", x.string()};
      else if (!findInPath("cmake").empty()) { argv = {"cmake", "-E", "tar", "xf", archives[i].string()}; cwd = x; }
      else { cleanup(); return fail("cannot unpack a .zip: install unzip or bsdtar (libarchive)"); }
#endif
    } else {
      argv = {"tar", "-xf", archives[i].string(), "-C", x.string()};
    }
    std::string out;
    int rc = runTool(argv, {}, cwd, out, cancel, {});
    if (rc == kCancelled) { cleanup(); return fail("install of " + m.name + " was cancelled"); }
    if (rc != 0) { cleanup(); return fail("unpacking " + baseName(p.url) + " failed (" + argv[0] + (rc < 0 ? " not found" : ": " + lastLine(out)) + ")"); }
    fs::path cur = x;
    for (int s = 0; s < p.strip; ++s) {
      std::vector<fs::path> kids;
      for (fs::directory_iterator it(cur, ec), end; !ec && it != end; it.increment(ec)) kids.push_back(it->path());
      if (kids.size() != 1 || !fs::is_directory(kids[0])) {
        cleanup();
        return fail(baseName(p.url) + " does not have the layout the manifest expects (strip " + std::to_string(p.strip) + "); the manifest needs fixing");
      }
      cur = kids[0];
    }
    fs::path target = p.dest.empty() || p.dest == "." ? stage : stage / p.dest;
    fs::create_directories(target, ec);
    std::vector<fs::path> kids;
    for (fs::directory_iterator it(cur, ec), end; !ec && it != end; it.increment(ec)) kids.push_back(it->path());
    for (const auto& k : kids) {
      std::string err;
      if (!moveMerge(k, target / k.filename(), err)) { cleanup(); return fail(err); }
    }
  }
  if (cancelled()) { cleanup(); return fail("install of " + m.name + " was cancelled"); }

  // atomic switch: the final root either does not exist or is complete
  const fs::path finalRoot = root(m.id);
  fs::path old;
  if (fs::exists(finalRoot, ec)) {
    old = dir_ / (".old-" + m.id + "-" + stamp);
    fs::remove_all(old, ec);
    fs::rename(finalRoot, old, ec);
    if (ec) { cleanup(); return fail("cannot replace " + finalRoot.string() + ": " + ec.message()); }
  }
  fs::rename(stage, finalRoot, ec);
  if (ec) {
    if (!old.empty()) fs::rename(old, finalRoot, ec);
    cleanup();
    return fail("cannot move the unpacked module into place: " + ec.message());
  }
  cleanup();
  if (!old.empty()) fs::remove_all(old, ec);

  auto undo = [&]() { std::error_code e; fs::remove_all(finalRoot, e); };
  report("check", 0, 0, "");
  std::string out;
  if (!runCheck(m, out)) {
    undo();
    return fail(m.name + " was unpacked but its check command failed (" + out + "). It was removed again.");
  }
  // postInstall steps run with the module environment (dependencies included); licenses were accepted up front.
  if (!m.postInstall.empty()) {
    std::set<std::string> seen;
    ModuleEnv env;
    std::function<void(const Module&)> collect = [&](const Module& mm) {
      if (seen.count(mm.id)) return;
      seen.insert(mm.id);
      for (const auto& r : mm.requires_)
        if (const Module* d = catalog_.find(r)) collect(*d);
      for (const auto& kv : mm.env) env.vars.emplace_back(kv.first, expand(kv.second, mm));
      std::vector<std::string> own;
      for (const auto& p : mm.path) own.push_back(expand(p, mm));
      env.pathDirs.insert(env.pathDirs.begin(), own.begin(), own.end());
    };
    collect(m);
    for (const auto& step : m.postInstall) {
      std::vector<std::string> argv;
      for (const auto& a : step.argv) argv.push_back(expand(a, m));
      if (argv.empty()) continue;
      report("postinstall", 0, 0, argv[0]);
      const char* inherited = std::getenv("PATH");
      std::string o;
      int rc = runTool(argv, env.flatten(inherited ? inherited : ""), finalRoot, o, cancel, {});
      if (rc != 0) {
        undo();
        return fail(rc == kCancelled ? "install of " + m.name + " was cancelled" : m.name + " post-install step failed (" + baseName(argv[0]) + ": " + lastLine(o) + "). It was removed again.");
      }
    }
  }
  auto recs = installed();
  recs.erase(std::remove_if(recs.begin(), recs.end(), [&](const InstalledRecord& r) { return r.id == m.id; }), recs.end());
  recs.push_back({m.id, m.version, host_, finalRoot.string()});
  writeInstalled(recs);
  return {true, ""};
}

Result ModuleManager::remove(const std::string& id) {
  const Module* m = catalog_.find(id);
  if (!m) return {false, "unknown module '" + id + "'"};
  auto recs = installed();
  bool had = false;
  for (const auto& r : recs) had = had || r.id == id;
  if (!had) return {false, m->name + " is not installed."};
  for (const Module& other : catalog_.all())
    if (other.id != id && isInstalled(other.id) &&
        std::find(other.requires_.begin(), other.requires_.end(), id) != other.requires_.end())
      return {false, other.name + " needs " + m->name + ". Remove " + other.id + " first."};
  std::error_code ec;
  fs::remove_all(root(id), ec);
  if (ec) return {false, "cannot delete " + root(id).string() + ": " + ec.message()};
  recs.erase(std::remove_if(recs.begin(), recs.end(), [&](const InstalledRecord& r) { return r.id == id; }), recs.end());
  writeInstalled(recs);
  return {true, m->name + " was removed."};
}

Result ModuleManager::verify(const std::string& id) const {
  const Module* m = catalog_.find(id);
  if (!m) return {false, "unknown module '" + id + "'"};
  if (!isInstalled(id)) return {false, m->name + " is not installed. Install it from Hub > Modules or run: rensga modules install " + id};
  for (const auto& r : m->requires_)
    if (!isInstalled(r)) return {false, m->name + " needs " + r + ", which is not installed."};
  std::string out;
  if (!runCheck(*m, out)) return {false, m->name + ": check command failed (" + out + ")"};
  return {true, m->name + " is OK (" + out + ")"};
}

}  // namespace modules
}  // namespace rensga
