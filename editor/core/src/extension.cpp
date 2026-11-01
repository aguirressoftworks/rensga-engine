#include "rensga/editor/extension.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <system_error>

#include "rensga/editor/data_asset_doc.h"
#include "rensga/editor/editor_log.h"
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
namespace ext {

namespace fs = std::filesystem;
using nlohmann::json;

// ---- registry -----------------------------------------------------------------------------------------------------------

namespace {
struct Registry {
  std::vector<InspectorEntry> inspectors;
  std::vector<WindowEntry> windows;
  std::vector<MenuEntry> menus;
  std::map<std::string, bool> open;
  unsigned seq = 1;
};
Registry& reg() {
  static Registry r;
  return r;
}
}  // namespace

void addInspector(const char* type, InspectorFn fn, bool replaceDefault) {
  if (!type || !fn) return;
  Registry& r = reg();
  r.inspectors.push_back({type, fn, replaceDefault, r.seq++});
}
void addWindow(const char* title, VoidFn fn) {
  if (!title || !*title || !fn) return;
  Registry& r = reg();
  r.windows.push_back({title, fn, r.seq++});
}
void addMenuItem(const char* path, const char* shortcut, VoidFn fn) {
  if (!path || !*path || !fn) return;
  Registry& r = reg();
  r.menus.push_back({path, shortcut ? shortcut : "", fn, r.seq++});
}
const std::vector<InspectorEntry>& inspectors() { return reg().inspectors; }
const std::vector<WindowEntry>& windows() { return reg().windows; }
const std::vector<MenuEntry>& menuItems() { return reg().menus; }
unsigned registrationMark() { return reg().seq; }
bool& windowOpen(const std::string& title) { return reg().open[title]; }

bool inspectorReplaces(const std::string& type) {
  for (const InspectorEntry& e : reg().inspectors)
    if (e.replaceDefault && e.type == type) return true;
  return false;
}

void unregisterFrom(unsigned mark) {
  Registry& r = reg();
  auto old = [&](unsigned s) { return s >= mark; };
  r.inspectors.erase(std::remove_if(r.inspectors.begin(), r.inspectors.end(), [&](const InspectorEntry& e) { return old(e.seq); }), r.inspectors.end());
  r.windows.erase(std::remove_if(r.windows.begin(), r.windows.end(), [&](const WindowEntry& e) { return old(e.seq); }), r.windows.end());
  r.menus.erase(std::remove_if(r.menus.begin(), r.menus.end(), [&](const MenuEntry& e) { return old(e.seq); }), r.menus.end());
}

// ---- ComponentView ------------------------------------------------------------------------------------------------------

std::string ComponentView::ObjectName() const {
  if (refs_.empty()) return std::string();
  const SceneObject* o = doc_.find(refs_[0].guid);
  return o ? o->name : std::string();
}

const TypeInfo* ComponentView::TypeOf() const { return TypeRegistry::instance().find(type_.c_str()); }

bool ComponentView::Mixed(const char* field) const { return field && fieldMixed(doc_, refs_, field); }

json ComponentView::Get(const char* field) const {
  if (refs_.empty() || !field) return json();
  return fieldValue(doc_, refs_[0], field);
}
float ComponentView::GetFloat(const char* field, float fallback) const {
  const json v = Get(field);
  return v.is_number() ? v.get<float>() : fallback;
}
int ComponentView::GetInt(const char* field, int fallback) const {
  const json v = Get(field);
  if (v.is_number_integer()) return v.get<int>();
  if (v.is_number()) return static_cast<int>(v.get<double>());
  return fallback;
}
bool ComponentView::GetBool(const char* field, bool fallback) const {
  const json v = Get(field);
  return v.is_boolean() ? v.get<bool>() : fallback;
}
std::string ComponentView::GetString(const char* field) const {
  const json v = Get(field);
  return v.is_string() ? v.get<std::string>() : std::string();
}
std::string ComponentView::GetGuid(const char* field) const { return GetString(field); }
bool ComponentView::GetVector3(const char* field, float out[3]) const {
  const json v = Get(field);
  if (!v.is_array() || v.size() != 3) return false;
  for (int i = 0; i < 3; ++i) {
    if (!v[i].is_number()) return false;
    out[i] = v[i].get<float>();
  }
  return true;
}
bool ComponentView::Set(const char* field, const json& value) {
  if (refs_.empty() || !field) return false;
  const TypeInfo* ti = TypeOf();
  bool known = false;
  for (u32 i = 0; ti && i < ti->fieldCount; ++i) known = known || std::string(ti->fields[i].name) == field;
  if (!known) return false;
  return editFields(doc_, refs_, field, value).ok;
}

// ---- editor API ---------------------------------------------------------------------------------------------------------

Host& host() {
  static Host h;
  return h;
}

namespace {
SceneDocument* doc() { return host().doc && host().doc->loaded() ? host().doc : nullptr; }
const SceneObject* objectBy(SceneDocument& d, const std::string& nameOrGuid) {
  if (const SceneObject* o = d.find(nameOrGuid)) return o;
  for (const SceneObject& o : d.scene().objects)
    if (o.name == nameOrGuid) return &o;
  return nullptr;
}
int componentIndex(const SceneObject& o, const std::string& type) {
  for (size_t i = 0; i < o.components.size(); ++i)
    if (o.components[i].type == type) return static_cast<int>(i);
  return -1;
}
}  // namespace

namespace Selection {
std::vector<std::string> Guids() { return doc() ? doc()->selection() : std::vector<std::string>(); }
std::vector<std::string> Names() {
  std::vector<std::string> out;
  if (SceneDocument* d = doc())
    for (const std::string& g : d->selection())
      if (const SceneObject* o = d->find(g)) out.push_back(o->name);
  return out;
}
std::string ActiveGuid() { return doc() ? doc()->selected() : std::string(); }
bool Select(const std::string& nameOrGuid) {
  SceneDocument* d = doc();
  const SceneObject* o = d ? objectBy(*d, nameOrGuid) : nullptr;
  if (!o) return false;
  d->select(o->guid);
  return true;
}
bool SelectMany(const std::vector<std::string>& guids) {
  SceneDocument* d = doc();
  if (!d) return false;
  d->setSelection(guids);
  return true;
}
}  // namespace Selection

namespace SceneEdit {
std::vector<ObjectInfo> Find(const std::string& nameContains, const std::string& withComponent) {
  std::vector<ObjectInfo> out;
  SceneDocument* d = doc();
  if (!d) return out;
  for (const SceneObject& o : d->scene().objects) {
    if (!nameContains.empty() && o.name.find(nameContains) == std::string::npos) continue;
    if (!withComponent.empty() && componentIndex(o, withComponent) < 0) continue;
    out.push_back({o.guid, o.name, o.active});
  }
  return out;
}
std::string NameOf(const std::string& guid) {
  SceneDocument* d = doc();
  const SceneObject* o = d ? d->find(guid) : nullptr;
  return o ? o->name : std::string();
}
json GetField(const std::string& objectGuid, const std::string& type, const std::string& field) {
  SceneDocument* d = doc();
  const SceneObject* o = d ? d->find(objectGuid) : nullptr;
  const int ci = o ? componentIndex(*o, type) : -1;
  if (ci < 0) return json();
  ComponentRef r;
  r.guid = objectGuid;
  r.index = ci;
  return fieldValue(*d, r, field);
}
bool SetField(const std::string& objectGuid, const std::string& type, const std::string& field, const json& value) {
  SceneDocument* d = doc();
  const SceneObject* o = d ? d->find(objectGuid) : nullptr;
  const int ci = o ? componentIndex(*o, type) : -1;
  if (ci < 0) return false;
  ComponentRef r;
  r.guid = objectGuid;
  r.index = ci;
  return editFields(*d, {r}, field, value).ok;
}
bool SetPosition(const std::string& objectGuid, float x, float y, float z) {
  SceneDocument* d = doc();
  const SceneObject* o = d ? d->find(objectGuid) : nullptr;
  if (!o) return false;
  const float p[3] = {x, y, z};
  return d->setTransform(objectGuid, p, o->rotation, o->scale);
}
bool GetPosition(const std::string& objectGuid, float out[3]) {
  SceneDocument* d = doc();
  const SceneObject* o = d ? d->find(objectGuid) : nullptr;
  if (!o) return false;
  for (int i = 0; i < 3; ++i) out[i] = o->position[i];
  return true;
}
void BeginGroup() { if (SceneDocument* d = doc()) d->beginGesture(); }
void EndGroup() { if (SceneDocument* d = doc()) d->endGesture(); }
bool Undo() { SceneDocument* d = doc(); return d && d->undo(); }
bool Redo() { SceneDocument* d = doc(); return d && d->redo(); }
}  // namespace SceneEdit

namespace Assets {
std::vector<AssetInfo> Find(const std::string& nameContains, const std::string& kind) {
  std::vector<AssetInfo> out;
  const AssetDatabase* db = host().assets;
  if (!db) return out;
  for (const AssetEntry& e : db->entries()) {
    if (e.kind == AssetKind::Folder) continue;
    if (!kind.empty() && kind != assetKindName(e.kind)) continue;
    if (!nameContains.empty() && e.rel.find(nameContains) == std::string::npos) continue;
    out.push_back({e.rel, e.guid, assetKindName(e.kind)});
  }
  return out;
}
std::string PathOf(const std::string& guid) {
  const AssetDatabase* db = host().assets;
  const AssetEntry* e = db ? db->findGuid(guid) : nullptr;
  return e ? e->rel : std::string();
}
std::string GuidOf(const std::string& path) {
  const AssetDatabase* db = host().assets;
  const AssetEntry* e = db ? db->findPath(path) : nullptr;
  return e ? e->guid : std::string();
}
std::string ProjectRoot() { return host().projectRoot.string(); }
json ReadDataAsset(const std::string& guid) {
  const AssetDatabase* db = host().assets;
  const AssetEntry* e = db ? db->findGuid(guid) : nullptr;
  if (!e || e->kind != AssetKind::DataAsset) return json();
  DataAssetDoc d;
  std::string err;
  if (!d.load(e->path, err) || d.typeName().empty()) return json();
  json out = json::object();
  out["type"] = d.typeName();
  out["fields"] = d.values();
  return out;
}
bool SetDataAssetField(const std::string& guid, const std::string& field, const json& value) {
  const AssetDatabase* db = host().assets;
  const AssetEntry* e = db ? db->findGuid(guid) : nullptr;
  DataAssetDoc d;
  std::string err;
  if (!e || e->kind != AssetKind::DataAsset) err = "not a data asset: " + guid;
  else if (d.load(e->path, err) && d.set(field, value, err)) return true;
  EditorLog::instance().warn("SetDataAssetField: " + err);
  return false;
}
}  // namespace Assets

namespace Log {
void Info(const std::string& t) { EditorLog::instance().info(t); }
void Warning(const std::string& t) { EditorLog::instance().warn(t); }
void Error(const std::string& t) { EditorLog::instance().error(t); }
void Status(const std::string& t) { if (host().setStatus) host().setStatus(t); }
}  // namespace Log

// ---- module layout and build ----------------------------------------------------------------------------------------------

namespace {
const char* kLibName =
#if defined(_WIN32)
    "rensga_editor_ext.dll";
#else
    "librensga_editor_ext.so";
#endif

bool isSource(const fs::path& p) {
  const std::string e = p.extension().string();
  return e == ".h" || e == ".hpp" || e == ".cpp";
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

int buildJobs() {
  const char* j = std::getenv("RENSGA_JOBS");
  const int n = j ? std::atoi(j) : 2;
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
  return _pclose(p) == 0;
#else
  return pclose(p) == 0;
#endif
}

long selfPid() {
#if defined(_WIN32)
  return static_cast<long>(GetCurrentProcessId());
#else
  return static_cast<long>(getpid());
#endif
}

bool pidAlive(long pid) {
#if defined(_WIN32)
  (void)pid;
  return true;
#else
  return pid > 0 && (kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
#endif
}

fs::file_time_type newestSource(const fs::path& dir, bool& any, bool (*want)(const fs::path&)) {
  fs::file_time_type best = fs::file_time_type::min();
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return best;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || !want(it->path())) continue;
    const auto t = fs::last_write_time(it->path(), ec);
    if (ec) continue;
    any = true;
    best = std::max(best, t);
  }
  return best;
}
bool headersOrCmake(const fs::path& p) { return p.extension() == ".h" || p.extension() == ".hpp" || p.filename() == "CMakeLists.txt"; }
}  // namespace

ExtensionLayout extensionLayout(const fs::path& projectRootIn) {
  std::error_code ec;
  const fs::path root = fs::absolute(projectRootIn, ec);
  ExtensionLayout l;
  l.sourceDir = fs::path(RENSGA_SOURCE_DIR) / "editor" / "data" / "editor_module";
  l.editorDir = root / "Editor";
  l.buildDir = root / "Library" / "EditorBuild";
  l.output = root / "Library" / "EditorAssemblies" / kLibName;
  std::vector<std::string> prefix;
#if !defined(_WIN32)
  prefix = {"nice", "-n", "19"};
#endif
  std::vector<std::string> configure = prefix;
  for (const char* a : {RENSGA_CMAKE_COMMAND, "-S"}) configure.push_back(a);
  configure.push_back(l.sourceDir.string());
  configure.push_back("-B");
  configure.push_back(l.buildDir.string());
  configure.push_back("-DRENSGA_PROJECT_DIR=" + root.string());
  configure.push_back(std::string("-DRENSGA_SOURCE_DIR=") + RENSGA_SOURCE_DIR);
  configure.push_back("-DRENSGA_EXT_OUTPUT_DIR=" + (root / "Library" / "EditorAssemblies").string());
  configure.push_back(std::string("-DCMAKE_BUILD_TYPE=") + RENSGA_SCRIPT_BUILD_TYPE);
#if defined(RENSGA_EDITOR_IMPORT_LIB)
  configure.push_back(std::string("-DRENSGA_EDITOR_IMPORT_LIB=") + RENSGA_EDITOR_IMPORT_LIB);
#endif
  std::vector<std::string> build = prefix;
  build.push_back(RENSGA_CMAKE_COMMAND);
  build.push_back("--build");
  build.push_back(l.buildDir.string());
  build.push_back("-j" + std::to_string(buildJobs()));
  l.commands = {configure, build};
  return l;
}

bool projectHasEditorCode(const fs::path& projectRoot) {
  bool any = false;
  newestSource(projectRoot / "Editor", any, isSource);
  return any;
}

std::string hashEditorSources(const fs::path& projectRoot) {
  std::error_code ec;
  std::vector<std::string> parts;
  const fs::path dir = projectRoot / "Editor";
  if (!fs::is_directory(dir, ec)) return std::string();
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || !isSource(it->path())) continue;
    const auto t = fs::last_write_time(it->path(), ec).time_since_epoch().count();
    parts.push_back(fs::relative(it->path(), dir, ec).generic_string() + ":" + std::to_string(fs::file_size(it->path(), ec)) + ":" + std::to_string(static_cast<long long>(t)));
  }
  std::sort(parts.begin(), parts.end());
  std::string out;
  for (const std::string& p : parts) out += p + ";";
  return out;
}

bool extensionModuleUpToDate(const fs::path& projectRoot) {
  const ExtensionLayout l = extensionLayout(projectRoot);
  std::error_code ec;
  if (!fs::is_regular_file(l.output, ec)) return false;
  const auto built = fs::last_write_time(l.output, ec);
  if (ec) return false;
  bool any = false;
  auto newest = newestSource(l.editorDir, any, isSource);
  newest = std::max(newest, newestSource(l.sourceDir, any, headersOrCmake));
  // The editor API the module was compiled against: a header change may change the ABI.
  newest = std::max(newest, newestSource(fs::path(RENSGA_SOURCE_DIR) / "editor" / "core" / "include", any, headersOrCmake));
  newest = std::max(newest, newestSource(fs::path(RENSGA_SOURCE_DIR) / "engine" / "core" / "include", any, headersOrCmake));
  return built >= newest;
}

bool buildExtensionModuleBlocking(const fs::path& projectRoot, std::string& log) {
  const ExtensionLayout l = extensionLayout(projectRoot);
  std::error_code ec;
  fs::create_directories(l.buildDir, ec);
  fs::create_directories(l.output.parent_path(), ec);
  for (const auto& argv : l.commands)
    if (!runArgv(argv, log)) return false;
  return fs::is_regular_file(l.output, ec);
}

// ---- ExtensionModule ----------------------------------------------------------------------------------------------------

ExtensionModule& ExtensionModule::instance() {
  static ExtensionModule m;
  return m;
}

int ExtensionModule::inspectorCount() const {
  int n = 0;
  for (const InspectorEntry& e : inspectors()) n += loaded() && e.seq >= mark_;
  return n;
}
int ExtensionModule::windowCount() const {
  int n = 0;
  for (const WindowEntry& e : windows()) n += loaded() && e.seq >= mark_;
  return n;
}
int ExtensionModule::menuCount() const {
  int n = 0;
  for (const MenuEntry& e : menuItems()) n += loaded() && e.seq >= mark_;
  return n;
}

bool ExtensionModule::loadShadow(const fs::path& lib, std::string& err) {
  std::error_code ec;
  const fs::path dir = lib.parent_path() / "shadow";
  fs::create_directories(dir, ec);
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string n = it->path().filename().string();
    const size_t at = n.find(".p");
    const long pid = at == std::string::npos ? 0 : std::atol(n.c_str() + at + 2);
    if (it->path() == lastGood_ || it->path() == shadow_) continue;
    if (pid != selfPid() && pidAlive(pid)) continue;
    fs::remove(it->path(), ec);
  }
  const fs::path copy = dir / (lib.stem().string() + ".p" + std::to_string(selfPid()) + ".g" + std::to_string(generation_ + 1) + lib.extension().string());
  fs::copy_file(lib, copy, fs::copy_options::overwrite_existing, ec);
  if (ec) { err = "cannot copy " + lib.string() + ": " + ec.message(); return false; }
  const unsigned mark = registrationMark();
  void* h = shlib::open(copy.string(), err);
  if (!h) {
    unregisterFrom(mark);  // a constructor may have registered before the load failed
    fs::remove(copy, ec);
    return false;
  }
  handle_ = h;
  mark_ = mark;
  shadow_ = copy;
  origin_ = lib;
  ++generation_;
  return true;
}

bool ExtensionModule::load(const fs::path& lib, std::string& err) {
  if (handle_) { err = "an editor extension module is already loaded"; return false; }
  std::error_code ec;
  if (!fs::is_regular_file(lib, ec)) { err = "no such library: " + lib.string(); return false; }
  if (!loadShadow(lib, err)) return false;
  lastGood_ = shadow_;
  return true;
}

void ExtensionModule::doUnload() {
  if (!handle_) return;
  unregisterFrom(mark_);  // before dlclose: no entry may keep a pointer into the library
  shlib::close(handle_);
  handle_ = nullptr;
  shadow_.clear();
}

void ExtensionModule::unload() {
  doUnload();
  std::error_code ec;
  if (!lastGood_.empty()) fs::remove(lastGood_, ec);
  lastGood_.clear();
}

bool ExtensionModule::reload(const fs::path& lib, std::string& err) {
  const fs::path previous = lastGood_;
  doUnload();
  std::error_code ec;
  if (!fs::is_regular_file(lib, ec)) err = "no such library: " + lib.string();
  else if (loadShadow(lib, err)) {
    if (!previous.empty() && previous != shadow_) fs::remove(previous, ec);
    lastGood_ = shadow_;
    return true;
  }
  std::string ignored;
  if (!previous.empty() && fs::is_regular_file(previous, ec)) {  // keep the tools working with the last good build
    const unsigned mark = registrationMark();
    if (void* h = shlib::open(previous.string(), ignored)) {
      handle_ = h;
      mark_ = mark;
      shadow_ = previous;
      ++generation_;
    }
  }
  return false;
}

}  // namespace ext
}  // namespace editor
}  // namespace rensga
