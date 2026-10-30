#include "rensga/editor/game_module.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <system_error>

#include "rensga/core/reflect_json.h"
#include "rensga/editor/editor_log.h"
#include "rensga/core/world.h"
#include "shared_library.h"

#if !defined(_WIN32)
#include <signal.h>
#include <unistd.h>
#endif

#ifndef RENSGA_SOURCE_DIR
#define RENSGA_SOURCE_DIR "."
#endif
#ifndef RENSGA_CMAKE_COMMAND
#define RENSGA_CMAKE_COMMAND "cmake"
#endif
#ifndef RENSGA_SCRIPT_BUILD_TYPE
#define RENSGA_SCRIPT_BUILD_TYPE "Debug"
#endif

namespace rensga {
namespace editor {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const char* kLibName =
#if defined(_WIN32)
    "rensga_game.dll";
#else
    "librensga_game.so";
#endif

fs::path assembliesDir(const fs::path& root) { return root / "Library" / "ScriptAssemblies"; }

bool isScriptSource(const fs::path& p) {
  const std::string e = p.extension().string();
  return e == ".h" || e == ".hpp" || e == ".cpp";
}

// Newest mtime among the files under `dir` accepted by `want`.
fs::file_time_type newestIn(const fs::path& dir, bool (*want)(const fs::path&), bool& any) {
  fs::file_time_type best = fs::file_time_type::min();
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return best;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || !want(it->path())) continue;
    auto t = fs::last_write_time(it->path(), ec);
    if (ec) continue;
    any = true;
    if (t > best) best = t;
  }
  return best;
}

bool headerOrCmake(const fs::path& p) {
  const std::string e = p.extension().string();
  return e == ".h" || e == ".hpp" || p.filename() == "CMakeLists.txt";
}

std::string shellQuote(const std::string& s) {
#if defined(_WIN32)
  return "\"" + s + "\"";
#else
  std::string o = "'";
  for (char c : s) o += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return o + "'";
#endif
}

int jobs() {
  const char* j = std::getenv("RENSGA_JOBS");
  int n = j ? std::atoi(j) : 2;
  return n > 0 ? n : 2;
}

bool runArgv(const std::vector<std::string>& argv, std::string& log) {
  std::string cmd;
  for (const std::string& a : argv) cmd += (cmd.empty() ? "" : " ") + shellQuote(a);
  cmd += " 2>&1";
#if defined(_WIN32)
  FILE* p = _popen(cmd.c_str(), "r");
#else
  FILE* p = popen(cmd.c_str(), "r");
#endif
  if (!p) { log += "cannot start: " + cmd + "\n"; return false; }
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, p)) > 0) log.append(buf, n);
#if defined(_WIN32)
  int rc = _pclose(p);
#else
  int rc = pclose(p);
#endif
  return rc == 0;
}

bool pidAlive(long pid) {
#if defined(_WIN32)
  (void)pid;
  return true;  // removal of a locked shadow simply fails
#else
  return pid > 0 && (kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
#endif
}

long selfPid() {
#if defined(_WIN32)
  return static_cast<long>(GetCurrentProcessId());
#else
  return static_cast<long>(getpid());
#endif
}

}  // namespace

ScriptModuleLayout scriptModuleLayout(const fs::path& projectRootIn) {
  // Absolute: the commands run from another working directory and CMake reads these as paths relative to its own.
  std::error_code absEc;
  const fs::path projectRoot = fs::absolute(projectRootIn, absEc);
  ScriptModuleLayout l;
  l.sourceDir = fs::path(RENSGA_SOURCE_DIR) / "editor" / "data" / "script_module";
  l.buildDir = projectRoot / "Library" / "ScriptBuild";
  l.output = assembliesDir(projectRoot) / kLibName;
  std::vector<std::string> prefix;
#if !defined(_WIN32)
  prefix = {"nice", "-n", "19"};  // never starve the editor UI or the machine
#endif
  std::vector<std::string> configure = prefix;
  for (const char* a : {RENSGA_CMAKE_COMMAND, "-S"}) configure.push_back(a);
  configure.push_back(l.sourceDir.string());
  configure.push_back("-B");
  configure.push_back(l.buildDir.string());
  configure.push_back("-DRENSGA_PROJECT_DIR=" + projectRoot.string());
  configure.push_back(std::string("-DRENSGA_SOURCE_DIR=") + RENSGA_SOURCE_DIR);
  configure.push_back("-DRENSGA_SCRIPT_OUTPUT_DIR=" + assembliesDir(projectRoot).string());
  configure.push_back(std::string("-DCMAKE_BUILD_TYPE=") + RENSGA_SCRIPT_BUILD_TYPE);
#if defined(RENSGA_EDITOR_IMPORT_LIB)
  configure.push_back(std::string("-DRENSGA_EDITOR_IMPORT_LIB=") + RENSGA_EDITOR_IMPORT_LIB);
#endif
  std::vector<std::string> build = prefix;
  build.push_back(RENSGA_CMAKE_COMMAND);
  build.push_back("--build");
  build.push_back(l.buildDir.string());
  build.push_back("-j" + std::to_string(jobs()));
  l.commands = {configure, build};
  return l;
}

bool projectHasScripts(const fs::path& projectRoot) {
  bool any = false;
  newestIn(projectRoot / "Assets" / "Scripts", isScriptSource, any);
  return any;
}

bool scriptModuleUpToDate(const fs::path& projectRoot) {
  const ScriptModuleLayout l = scriptModuleLayout(projectRoot);
  std::error_code ec;
  if (!fs::is_regular_file(l.output, ec)) return false;
  const auto built = fs::last_write_time(l.output, ec);
  if (ec) return false;
  bool any = false;
  auto newest = newestIn(projectRoot / "Assets" / "Scripts", isScriptSource, any);
  newest = std::max(newest, newestIn(l.sourceDir, headerOrCmake, any));
  const fs::path engine = fs::path(RENSGA_SOURCE_DIR) / "engine";  // header changes may change the ABI
  newest = std::max(newest, newestIn(engine / "core" / "include", headerOrCmake, any));
  newest = std::max(newest, newestIn(engine / "api" / "include", headerOrCmake, any));
  return built >= newest;
}

bool buildScriptModuleBlocking(const fs::path& projectRoot, std::string& log) {
  const ScriptModuleLayout l = scriptModuleLayout(projectRoot);
  std::error_code ec;
  fs::create_directories(l.buildDir, ec);
  fs::create_directories(assembliesDir(projectRoot), ec);
  for (const auto& argv : l.commands)
    if (!runArgv(argv, log)) return false;
  return fs::is_regular_file(l.output, ec);
}

// ---- GameModule ------------------------------------------------------------------------------------------------

GameModule& GameModule::instance() {
  static GameModule m;
  return m;
}

std::vector<const TypeInfo*> GameModule::types() const {
  std::vector<const TypeInfo*> out;
  const TypeRegistry& reg = TypeRegistry::instance();
  for (u32 i = firstType_; handle_ && i < endType_; ++i)
    if (const TypeInfo* t = reg.byId(i)) out.push_back(t);
  return out;
}

bool GameModule::ownsType(const TypeInfo* t) const {
  return handle_ && t && t->id >= firstType_ && t->id < endType_;
}

int GameModule::addBeforeUnload(std::function<void()> fn) {
  hooks_.push_back({nextHook_, true, std::move(fn)});
  return nextHook_++;
}

int GameModule::addAfterLoad(std::function<void()> fn) {
  hooks_.push_back({nextHook_, false, std::move(fn)});
  return nextHook_++;
}

void GameModule::removeHook(int id) {
  hooks_.erase(std::remove_if(hooks_.begin(), hooks_.end(), [&](const Hook& h) { return h.id == id; }), hooks_.end());
}

void GameModule::runHooks(bool before) {
  // Copy: a hook may add or remove hooks.
  std::vector<Hook> copy = hooks_;
  for (Hook& h : copy)
    if (h.before == before && h.fn) h.fn();
}

bool GameModule::loadShadow(const fs::path& lib, std::string& err) {
  std::error_code ec;
  const fs::path dir = lib.parent_path() / "shadow";
  fs::create_directories(dir, ec);
  // Old shadows of dead processes (and of earlier loads here) are garbage.
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string n = it->path().filename().string();
    const size_t at = n.find(".p");
    long pid = at == std::string::npos ? 0 : std::atol(n.c_str() + at + 2);
    if (it->path() == lastGood_ || it->path() == shadow_) continue;
    if (pid != selfPid() && pidAlive(pid)) continue;
    fs::remove(it->path(), ec);
  }
  const fs::path copy = dir / (lib.stem().string() + ".p" + std::to_string(selfPid()) + ".g" +
                               std::to_string(generation_ + 1) + lib.extension().string());
  fs::copy_file(lib, copy, fs::copy_options::overwrite_existing, ec);
  if (ec) { err = "cannot copy " + lib.string() + " to " + copy.string() + ": " + ec.message(); return false; }
  TypeRegistry& reg = TypeRegistry::instance();
  const u32 mark = reg.mark();
  void* h = shlib::open(copy.string(), err);
  if (!h) {
    reg.unregisterFrom(mark);
    fs::remove(copy, ec);
    return false;
  }
  handle_ = h;
  firstType_ = mark;
  endType_ = reg.mark();
  shadow_ = copy;
  origin_ = lib;
  ++generation_;
  return true;
}

bool GameModule::load(const fs::path& lib, std::string& err) {
  if (handle_) { err = "a game module is already loaded"; return false; }
  std::error_code ec;
  if (!fs::is_regular_file(lib, ec)) { err = "no such library: " + lib.string(); return false; }
  if (!loadShadow(lib, err)) return false;
  lastGood_ = shadow_;
  runHooks(false);
  return true;
}

void GameModule::unload() {
  doUnload();
  std::error_code ec;
  if (!lastGood_.empty()) fs::remove(lastGood_, ec);
  lastGood_.clear();
}

void GameModule::doUnload() {
  if (!handle_) return;
  runHooks(true);
  TypeRegistry::instance().unregisterFrom(firstType_);
  shlib::close(handle_);
  handle_ = nullptr;
  firstType_ = endType_ = 0;
  shadow_.clear();
}

bool GameModule::reload(const fs::path& lib, std::string& err) {
  const fs::path previous = lastGood_;
  doUnload();
  std::error_code ec;
  if (!fs::is_regular_file(lib, ec)) err = "no such library: " + lib.string();
  else if (loadShadow(lib, err)) {
    if (!previous.empty() && previous != shadow_) fs::remove(previous, ec);
    lastGood_ = shadow_;
    runHooks(false);
    return true;
  }
  // The new library is unusable: bring the last good one back so scripts keep running.
  std::string ignored;
  if (!previous.empty() && fs::is_regular_file(previous, ec)) {
    TypeRegistry& reg = TypeRegistry::instance();
    const u32 mark = reg.mark();
    if (void* h = shlib::open(previous.string(), ignored)) {
      handle_ = h;
      firstType_ = mark;
      endType_ = reg.mark();
      shadow_ = previous;
      ++generation_;
      runHooks(false);
    }
  }
  return false;
}

// ---- project open ----------------------------------------------------------------------------------------------

static bool sameFile(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  return !a.empty() && !b.empty() && fs::weakly_canonical(a, ec) == fs::weakly_canonical(b, ec);
}

bool reloadGameModule(const fs::path& projectRoot, std::string& err) {
  GameModule& gm = GameModule::instance();
  const fs::path lib = scriptModuleLayout(projectRoot).output;
  return gm.loaded() ? gm.reload(lib, err) : gm.load(lib, err);
}

ModuleState ensureGameModule(const fs::path& projectRoot, bool blockingBuild) {
  EditorLog& log = EditorLog::instance();
  GameModule& gm = GameModule::instance();
  const fs::path lib = scriptModuleLayout(projectRoot).output;
  if (gm.loaded() && !sameFile(gm.origin(), lib)) gm.unload();  // another project's scripts
  if (!projectHasScripts(projectRoot)) { gm.unload(); return ModuleState::NoScripts; }
  const bool fresh = scriptModuleUpToDate(projectRoot);
  if (!fresh) {
    if (!blockingBuild) {
      // Interactive: the background script-build job builds it and calls reloadGameModule when done.
      log.info("scripts: module is out of date, a rebuild is needed");
      return gm.loaded() ? ModuleState::Loaded : ModuleState::NeedsBuild;
    }
    log.info("scripts: building the game module");
    std::string out;
    const bool ok = buildScriptModuleBlocking(projectRoot, out);
    if (!ok) {
      log.error("scripts: the game module did not build");
      size_t at = 0, shown = 0;
      while (at < out.size()) {
        size_t nl = out.find('\n', at);
        if (nl == std::string::npos) nl = out.size();
        const std::string line = out.substr(at, nl - at);
        if (line.find("error") != std::string::npos) { log.error(line); ++shown; }
        at = nl + 1;
      }
      if (shown == 0 && !out.empty()) {  // no compiler error line: show the tail so the cause is visible
        size_t from = out.size() > 600 ? out.size() - 600 : 0;
        log.error(out.substr(from));
      }
      return ModuleState::BuildFailed;
    }
  }
  std::string err;
  if (gm.loaded() && fresh && sameFile(gm.origin(), lib)) return ModuleState::Loaded;
  if (!reloadGameModule(projectRoot, err)) {
    log.error("scripts: cannot load the game module: " + err);
    return ModuleState::LoadFailed;
  }
  log.info("scripts: game module loaded (" + std::to_string(gm.types().size()) + " script type(s))");
  return ModuleState::Loaded;
}

// ---- script state ----------------------------------------------------------------------------------------------

json serializeScriptState(World& world) {
  json out = json::array();
  const GameModule& gm = GameModule::instance();
  for (u32 oi = 0; oi < world.objectHighWater(); ++oi) {
    GameObject* go = world.objectAt(oi);
    if (!go) continue;
    for (u32 ci = 0; ci < go->componentCount; ++ci) {
      const TypeInfo* ti = TypeRegistry::instance().byId(go->components[ci].typeId);
      if (!ti || !gm.ownsType(ti)) continue;
      const ComponentPool* p = world.pool(*ti);
      if (!p) continue;
      char guid[Guid::kTextLength + 1];
      go->guid.format(guid);
      out.push_back({{"object", guid}, {"index", ci}, {"type", ti->name},
                     {"fields", fieldsToJson(*ti, p->at(go->components[ci].slot))}});
    }
  }
  return out;
}

void restoreScriptState(World& world, const json& state) {
  if (!state.is_array()) return;
  for (const json& e : state) {
    if (!e.is_object() || !e.contains("object") || !e.contains("index") || !e.contains("type") || !e.contains("fields")) continue;
    Guid g;
    if (!e["object"].is_string() || !Guid::parse(e["object"].get<std::string>().c_str(), g)) continue;
    GameObject* go = world.findByGuid(g);
    const u32 ci = e["index"].get<u32>();
    if (!go || ci >= go->componentCount) continue;
    const TypeInfo* ti = TypeRegistry::instance().byId(go->components[ci].typeId);
    if (!ti || e["type"].get<std::string>() != ti->name) continue;
    const ComponentPool* p = world.pool(*ti);
    if (!p || !e["fields"].is_object()) continue;
    void* obj = p->at(go->components[ci].slot);
    // Field by field: a removed field is skipped, a field whose type changed keeps its default.
    for (u32 fi = 0; fi < ti->fieldCount; ++fi) {
      const char* name = ti->fields[fi].name;
      if (!e["fields"].contains(name)) continue;
      json one = json::object();
      one[name] = e["fields"][name];
      fieldsFromJson(*ti, obj, one, nullptr);
    }
  }
}

}  // namespace editor
}  // namespace rensga
