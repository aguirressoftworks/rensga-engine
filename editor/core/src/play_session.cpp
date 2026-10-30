#include "rensga/editor/play_session.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#include "rensga/core/components.h"
#include "rensga/core/log.h"
#include "rensga/core/reflect_json.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/game_module.h"
#include "rensga/editor/script_build.h"
#include "rensga/editor/target_module.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace rensga {
namespace editor {

namespace {

// Game log lines (Debug::Log, RLOG_*) of the running World go to the editor Console.
class ConsoleSink : public ILogSink {
 public:
  void write(const LogRecord& r) override {
    EditorLog::instance().add(LogSource::Game, r.level == ::rensga::LogLevel::Error ? ::rensga::editor::LogLevel::Error
                                                : r.level == ::rensga::LogLevel::Warn ? ::rensga::editor::LogLevel::Warn
                                                                            : ::rensga::editor::LogLevel::Info,
                              r.message);
  }
};
ConsoleSink g_sink;
int g_sinkUsers = 0;

void sinkAcquire() { if (g_sinkUsers++ == 0) log::addSink(&g_sink); }
void sinkRelease() { if (g_sinkUsers > 0 && --g_sinkUsers == 0) log::removeSink(&g_sink); }

std::string guidKey(const Guid& g) {
  char t[Guid::kTextLength + 1];
  g.format(t);
  return t;
}

}  // namespace

std::string playObjectKey(const GameObject& go) {
  if (!go.guid.isNull()) return guidKey(go.guid);
  return "runtime:" + std::to_string(go.index) + ":" + std::to_string(go.generation);
}

PlaySession::~PlaySession() {
  if (hookBefore_) GameModule::instance().removeHook(hookBefore_);
  if (hookAfter_) GameModule::instance().removeHook(hookAfter_);
  if (state_ != PlayState::Stopped) {
    world_.reset();
    if (doc_) doc_->setPlayMirror(nullptr, nullptr);
    sinkRelease();
  }
}

bool PlaySession::start(SceneDocument& doc, const fs::path& projectRoot, const std::string& target, std::string& err) {
  if (state_ != PlayState::Stopped) { err = "already playing"; return false; }
  if (!doc.loaded()) { err = "no scene is open"; return false; }
  if (scriptsHaveErrors()) { err = "fix the script compile errors first"; return false; }
  doc_ = &doc;
  root_ = projectRoot;
  target_ = target;
  snapDoc_ = doc.document();
  snapSelected_ = doc.selected();
  snapSelection_ = doc.selection();
  snapUndo_ = doc.undoDepth();
  snapDirty_ = doc.dirty();
  snapPath_ = doc.path();
  sinkAcquire();
  if (!buildWorld(err)) {
    sinkRelease();
    world_.reset();
    return false;
  }
  gameFrames_ = 0;
  accum_ = 0;
  stepRequests_ = 0;
  fpsAvg_ = 0;
  over_.clear();
  findings_.clear();
  doc.setPlayMirror(&mirror_, this);
  state_ = PlayState::Playing;
  GameModule& gm = GameModule::instance();
  hookBefore_ = gm.addBeforeUnload([this] { onBeforeUnload(); });
  hookAfter_ = gm.addAfterLoad([this] { onAfterLoad(); });
  checkBudgets(true);
  return true;
}

bool PlaySession::buildWorld(std::string& err) {
  world_ = std::make_unique<EditorWorld>();
  if (!world_->build(root_, target_, snapPath_, snapDoc_, err)) { world_.reset(); return false; }
  for (const CookMessage& m : world_->messages()) {
    if (m.severity == CookMessage::Warning)
      EditorLog::instance().warn("play: " + m.text + (m.asset.empty() ? std::string() : " [" + m.asset + "]"));
  }
  mirror_ = parseScene(snapDoc_);
  mirror_.path = snapPath_.string();
  mirrorIndex_.clear();
  runtimeOnly_.clear();
  for (size_t i = 0; i < mirror_.objects.size(); ++i) mirrorIndex_[mirror_.objects[i].guid] = i;
  refreshMirror(true);
  return true;
}

void PlaySession::setPaused(bool paused) {
  if (state_ == PlayState::Stopped) return;
  state_ = paused ? PlayState::Paused : PlayState::Playing;
  stepRequests_ = 0;
}

void PlaySession::tick(double dt, const VirtualGamepad& pad) {
  if (state_ == PlayState::Stopped) return;
  if (doc_ && !doc_->playing() && !reloading_) {  // the document was replaced under the session (another scene was opened)
    EditorLog::instance().warn("play: the open scene was replaced; the session ended without touching the new scene");
    GameModule& gm = GameModule::instance();
    if (hookBefore_) gm.removeHook(hookBefore_);
    if (hookAfter_) gm.removeHook(hookAfter_);
    hookBefore_ = hookAfter_ = 0;
    world_.reset();
    sinkRelease();
    state_ = PlayState::Stopped;
    doc_ = nullptr;
    mirror_ = Scene();
    return;
  }
  if (!world_ || !world_->valid()) return;
  if (dt > 0.25) dt = 0.25;
  if (dt > 0) fpsAvg_ = fpsAvg_ == 0 ? 1.0 / dt : fpsAvg_ * 0.9 + (1.0 / dt) * 0.1;
  stats_.fps = fpsAvg_;
  const auto t0 = std::chrono::steady_clock::now();
  bool ran = false;
  if (state_ == PlayState::Playing) {
    world_->frame(static_cast<Scalar>(dt), pad);
    ++gameFrames_;
    ran = true;
  } else {
    while (stepRequests_ > 0) {
      --stepRequests_;
      world_->step(pad);
      ++gameFrames_;
      ran = true;
    }
  }
  if (ran) {
    stats_.simMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    refreshMirror(false);
    checkBudgets(false);
  } else if (mirrorSelected_ != doc_->selected()) {
    refreshMirror(false);  // the selection changed while paused: show that object's live fields
  }
  stats_.gameFrames = gameFrames_;
}

GameObject* PlaySession::objectByKey(const std::string& key) const {
  if (!world_ || !world_->valid()) return nullptr;
  auto it = keyToObject_.find(key);
  if (it == keyToObject_.end()) return nullptr;
  return world_->world().objectAt(it->second);
}

void PlaySession::refreshMirror(bool) {
  if (!world_ || !world_->valid()) return;
  World& w = world_->world();
  const std::string selected = doc_ ? doc_->selected() : std::string();
  mirrorSelected_ = selected;
  keyToObject_.clear();
  std::vector<uint8_t> present(mirror_.objects.size(), 0);
  std::vector<SceneObject> added;
  uint32_t live = 0, runtime = 0;
  for (u32 i = 0; i < w.objectHighWater(); ++i) {
    GameObject* go = w.objectAt(i);
    if (!go || go->dying) continue;
    ++live;
    const std::string key = playObjectKey(*go);
    keyToObject_[key] = i;
    SceneObject* so = nullptr;
    auto it = mirrorIndex_.find(key);
    if (it != mirrorIndex_.end() && it->second < mirror_.objects.size()) {
      so = &mirror_.objects[it->second];
      present[it->second] = 1;
    } else {
      added.emplace_back();
      so = &added.back();
      so->guid = key;
      runtimeOnly_[key] = true;
    }
    const bool isRuntime = runtimeOnly_.count(key) != 0;
    if (isRuntime) ++runtime;
    so->name = isRuntime ? std::string(go->name) + " (runtime)" : std::string(go->name);
    const Transform* parent = go->transform.parent_;
    so->parentGuid = parent && parent->gameObject ? playObjectKey(*parent->gameObject) : std::string();
    so->active = go->selfActive_;
    so->tag = strcmp(go->tag, "Untagged") == 0 ? std::string() : std::string(go->tag);
    so->layer = go->layer;
    so->position[0] = go->transform.lpos_.x; so->position[1] = go->transform.lpos_.y; so->position[2] = go->transform.lpos_.z;
    so->rotation[0] = go->transform.lrot_.x; so->rotation[1] = go->transform.lrot_.y;
    so->rotation[2] = go->transform.lrot_.z; so->rotation[3] = go->transform.lrot_.w;
    so->scale[0] = go->transform.lscale_.x; so->scale[1] = go->transform.lscale_.y; so->scale[2] = go->transform.lscale_.z;
    const bool wantFields = key == selected;
    if (isRuntime || wantFields) {
      if (isRuntime && so->components.size() != go->componentCount) {
        so->components.clear();
        for (u32 ci = 0; ci < go->componentCount; ++ci) {
          const TypeInfo* ti = TypeRegistry::instance().byId(go->components[ci].typeId);
          SceneComponent sc;
          sc.type = ti ? ti->name : "?";
          so->components.push_back(std::move(sc));
        }
      }
      if (wantFields) {
        for (u32 ci = 0; ci < go->componentCount && ci < so->components.size(); ++ci) {
          const TypeInfo* ti = TypeRegistry::instance().byId(go->components[ci].typeId);
          const ComponentPool* p = ti ? w.pool(*ti) : nullptr;
          if (!p || so->components[ci].type != ti->name) continue;
          so->components[ci].fields = fieldsToJson(*ti, p->at(go->components[ci].slot));
        }
      }
    }
  }
  stats_.objects = live;
  stats_.runtimeObjects = runtime;
  bool removed = false;
  for (uint8_t p : present) if (!p) { removed = true; break; }
  if (removed || !added.empty()) {
    std::vector<SceneObject> next;
    next.reserve(mirror_.objects.size() + added.size());
    for (size_t i = 0; i < mirror_.objects.size(); ++i) {
      if (present[i]) next.push_back(std::move(mirror_.objects[i]));
      else runtimeOnly_.erase(mirror_.objects[i].guid);  // destroyed (a destroyed document object just disappears)
    }
    for (SceneObject& a : added) next.push_back(std::move(a));
    mirror_.objects = std::move(next);
    mirrorIndex_.clear();
    for (size_t i = 0; i < mirror_.objects.size(); ++i) mirrorIndex_[mirror_.objects[i].guid] = i;
  }
}

void PlaySession::finding(const std::string& key, bool over, const std::string& text) {
  bool& was = over_[key];
  if (over && !was) EditorLog::instance().warn("play: over budget: " + text);
  if (!over && was) EditorLog::instance().info("play: back within budget (" + key + ")");
  was = over;
  findings_.erase(std::remove_if(findings_.begin(), findings_.end(), [&](const BudgetFinding& f) { return f.key == key; }), findings_.end());
  if (over) findings_.push_back({key, text});
}

void PlaySession::checkBudgets(bool force) {
  if (!world_ || !world_->valid()) return;
  if (!force && (gameFrames_ % 10) != 0) return;  // cheap: every tenth frame
  const ConsoleTargetData& ct = world_->consoleTarget();
  World& w = world_->world();
  uint64_t fb = 0;
  for (const VramItem& it : framebufferVramItems(ct)) fb += it.bytes;
  stats_.vramUsed = fb + world_->textureBytes();
  stats_.vramCapacity = ct.vramBytes;
  stats_.memUsed = world_->packBytes() + world_->arenaUsed();
  stats_.memCapacity = ct.ramBytes;
  uint64_t tris = 0;
  const MeshTable& mt = world_->loaded().meshes;
  w.forEach<MeshRenderer>([&](MeshRenderer& mr) {
    GameObject* go = mr.gameObject();
    if (!go || mr.meshIndex == kInvalidIndex || mr.meshIndex >= mt.meshCount || !go->activeInHierarchy() || !mr.enabled()) return;
    tris += mt.meshes[mr.meshIndex].vertexCount / 3;
  });
  stats_.triangles = static_cast<uint32_t>(tris);
  stats_.trianglesBudget = ct.trianglesPerFrame;
  char b[200];
  if (stats_.vramCapacity) {
    snprintf(b, sizeof b, "VRAM %llu of %llu bytes (textures %llu + framebuffers %llu)", static_cast<unsigned long long>(stats_.vramUsed),
             static_cast<unsigned long long>(stats_.vramCapacity), static_cast<unsigned long long>(world_->textureBytes()),
             static_cast<unsigned long long>(fb));
    finding("vram", stats_.vramUsed > stats_.vramCapacity, b);
  }
  if (stats_.memCapacity) {
    snprintf(b, sizeof b, "memory %llu of %llu bytes (pack %zu + world %zu)", static_cast<unsigned long long>(stats_.memUsed),
             static_cast<unsigned long long>(stats_.memCapacity), world_->packBytes(), world_->arenaUsed());
    finding("memory", stats_.memUsed > stats_.memCapacity, b);
  }
  if (stats_.trianglesBudget) {
    snprintf(b, sizeof b, "%llu triangles per frame, budget %llu", static_cast<unsigned long long>(tris),
             static_cast<unsigned long long>(stats_.trianglesBudget));
    finding("triangles", tris > stats_.trianglesBudget, b);
  }
}

void PlaySession::stop(SceneDocument& doc) {
  if (state_ == PlayState::Stopped) return;
  GameModule& gm = GameModule::instance();
  if (hookBefore_) gm.removeHook(hookBefore_);
  if (hookAfter_) gm.removeHook(hookAfter_);
  hookBefore_ = hookAfter_ = 0;
  world_.reset();  // OnDisable / OnDestroy, memory released
  doc.setPlayMirror(nullptr, nullptr);
  sinkRelease();
  state_ = PlayState::Stopped;
  // The document was frozen, so it must equal the snapshot. If it does not, put the snapshot back.
  if (doc.document() != snapDoc_) {
    EditorLog::instance().warn("play: the scene document changed during play; restoring the snapshot");
    doc.restore(snapPath_, snapDoc_, snapDirty_);
  }
  doc.select(snapSelected_);
  for (const std::string& g : snapSelection_) if (g != snapSelected_) doc.toggleSelect(g);
  if (doc.undoDepth() != snapUndo_) EditorLog::instance().warn("play: undo history changed during play");
  mirror_ = Scene();
  mirrorIndex_.clear();
  keyToObject_.clear();
  runtimeOnly_.clear();
  findings_.clear();
  over_.clear();
  doc_ = nullptr;
  EditorLog::instance().info("play: stopped after " + std::to_string(gameFrames_) + " frames; scene restored");
}

// ---- hot reload ------------------------------------------------------------------------------------------------------
void PlaySession::onBeforeUnload() {
  if (!world_ || !world_->valid()) return;
  World& w = world_->world();
  savedScripts_ = serializeScriptState(w);
  savedTransforms_ = json::array();
  for (u32 i = 0; i < w.objectHighWater(); ++i) {
    GameObject* go = w.objectAt(i);
    if (!go || go->guid.isNull()) continue;
    const Transform& t = go->transform;
    savedTransforms_.push_back({{"guid", guidKey(go->guid)},
                                {"p", {t.lpos_.x, t.lpos_.y, t.lpos_.z}},
                                {"r", {t.lrot_.x, t.lrot_.y, t.lrot_.z, t.lrot_.w}},
                                {"s", {t.lscale_.x, t.lscale_.y, t.lscale_.z}},
                                {"a", go->selfActive_}});
  }
  world_.reset();  // script components of the old module must be destroyed while their types are still registered
  keyToObject_.clear();
  reloading_ = true;
}

void PlaySession::onAfterLoad() {
  if (!reloading_) return;
  reloading_ = false;
  std::string err;
  if (scriptsHaveErrors() || !buildWorld(err)) {
    EditorLog::instance().error("play: stopping, the scripts changed and the scene cannot be rebuilt: " + err);
    if (doc_) stop(*doc_);
    return;
  }
  World& w = world_->world();
  for (const json& e : savedTransforms_) {
    Guid g;
    if (!Guid::parse(e["guid"].get<std::string>().c_str(), g)) continue;
    GameObject* go = w.findByGuid(g);
    if (!go) continue;
    const auto& p = e["p"]; const auto& r = e["r"]; const auto& s = e["s"];
    go->transform.lpos_ = Vector3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
    go->transform.lrot_ = Quaternion(r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>());
    go->transform.lscale_ = Vector3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>());
    if (go->selfActive_ != e["a"].get<bool>()) w.setObjectActive(go, e["a"].get<bool>());
  }
  restoreScriptState(w, savedScripts_);
  refreshMirror(true);
  rebuilt_ = true;
  ++generation_;
  EditorLog::instance().info("play: scripts reloaded; running state kept (" + std::to_string(savedScripts_.size()) + " script components)");
}

// ---- PlayEditSink ------------------------------------------------------------------------------------------------------
bool PlaySession::transform(const std::string& guid, const float pos[3], const float rot[4], const float scale[3]) {
  GameObject* go = objectByKey(guid);
  if (!go) return false;
  go->transform.lpos_ = Vector3(pos[0], pos[1], pos[2]);
  go->transform.lrot_ = Quaternion(rot[0], rot[1], rot[2], rot[3]);
  go->transform.lscale_ = Vector3(scale[0], scale[1], scale[2]);
  auto it = mirrorIndex_.find(guid);
  if (it != mirrorIndex_.end()) {
    SceneObject& so = mirror_.objects[it->second];
    for (int i = 0; i < 3; ++i) { so.position[i] = pos[i]; so.scale[i] = scale[i]; }
    for (int i = 0; i < 4; ++i) so.rotation[i] = rot[i];
  }
  return true;
}

bool PlaySession::active(const std::string& guid, bool on) {
  GameObject* go = objectByKey(guid);
  if (!go || !world_) return false;
  world_->world().setObjectActive(go, on);
  auto it = mirrorIndex_.find(guid);
  if (it != mirrorIndex_.end()) mirror_.objects[it->second].active = on;
  return true;
}

bool PlaySession::field(const std::string& guid, int component, const std::string& fieldName, const json& value) {
  GameObject* go = objectByKey(guid);
  if (!go || !world_ || component < 0 || static_cast<u32>(component) >= go->componentCount) return false;
  const TypeInfo* ti = TypeRegistry::instance().byId(go->components[component].typeId);
  const ComponentPool* p = ti ? world_->world().pool(*ti) : nullptr;
  if (!p) return false;
  json one = json::object();
  one[fieldName] = value;
  std::string err;
  if (!fieldsFromJson(*ti, p->at(go->components[component].slot), one, &err)) {
    EditorLog::instance().warn("play: cannot set " + std::string(ti->name) + "." + fieldName + ": " + err);
    return false;
  }
  auto it = mirrorIndex_.find(guid);
  if (it != mirrorIndex_.end()) {
    SceneObject& so = mirror_.objects[it->second];
    if (static_cast<size_t>(component) < so.components.size()) {
      if (!so.components[component].fields.is_object()) so.components[component].fields = json::object();
      so.components[component].fields[fieldName] = value;
    }
  }
  return true;
}

}  // namespace editor
}  // namespace rensga
