#include "rensga/editor/edit_mode_world.h"

#include "rensga/core/application.h"
#include "rensga/core/gizmos.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/game_module.h"
#include "rensga/editor/play_session.h"

namespace fs = std::filesystem;

namespace rensga {
namespace editor {

EditModeWorld::~EditModeWorld() {
  release();
  if (hookBefore_) GameModule::instance().removeHook(hookBefore_);
  if (hookAfter_) GameModule::instance().removeHook(hookAfter_);
}

void EditModeWorld::hook() {
  if (hooked_) return;
  hooked_ = true;
  GameModule& gm = GameModule::instance();
  hookBefore_ = gm.addBeforeUnload([this] { release(); });  // the old types go away: no script component may survive
  hookAfter_ = gm.addAfterLoad([this] { stale_ = true; });
}

void EditModeWorld::release() {
  if (!world_) return;
  world_.reset();  // ~EditorWorld: OnDisable / OnDestroy, then the memory
  stale_ = true;
}

bool EditModeWorld::rebuild(const SceneDocument& doc, const fs::path& root, const std::string& target) {
  release();
  built_ = doc.document();
  path_ = doc.path();
  stale_ = false;
  auto w = std::make_unique<EditorWorld>();
  std::string err;
  if (!w->build(root, target, doc.path(), doc.document(), err)) {
    lastError_ = err;
    if (err != loggedError_) {  // once per distinct reason
      loggedError_ = err;
      EditorLog::instance().warn("edit-mode scripts: " + err);
    }
    return false;
  }
  lastError_.clear();
  loggedError_.clear();
  world_ = std::move(w);
  World& wd = world_->world();
  wd.setEditMode(true);
  Application::setPlaying(false);
  wd.startPending();  // Awake / OnEnable / Start of EXECUTE_IN_EDIT_MODE behaviours only
  wd.update();
  wd.lateUpdate();
  wd.flushDestroyed();
  rebuilds_++;
  return true;
}

bool EditModeWorld::sync(const SceneDocument& doc, const fs::path& root, const std::string& target) {
  hook();
  if (!doc.loaded() || doc.playing() || (requireModule_ && !GameModule::instance().loaded())) { release(); return false; }
  if (!stale_ && world_ && built_ == doc.document() && path_ == doc.path()) return true;
  seen_ = doc.document();
  sinceChange_ = 0;
  return rebuild(doc, root, target);
}

void EditModeWorld::update(const SceneDocument& doc, const fs::path& root, const std::string& target, double dt, bool suspended) {
  hook();
  if (suspended || !doc.loaded() || doc.playing() || (requireModule_ && !GameModule::instance().loaded())) {
    release();
    return;
  }
  sinceCheck_ += dt;
  sinceChange_ += dt;
  if (sinceCheck_ >= 0.1 || !world_) {  // the document comparison is not free: ten times a second is enough
    sinceCheck_ = 0;
    if (doc.path() != path_ || !(doc.document() == seen_)) {
      seen_ = doc.document();
      sinceChange_ = 0;
      stale_ = true;
    }
  }
  if (!stale_) return;
  const bool first = !world_ && built_.is_null();
  if (sinceChange_ >= debounce_ || first) rebuild(doc, root, target);
}

unsigned EditModeWorld::drawGizmos(World& world, const std::string& selectedKey) {
  struct Ctx { const std::string* sel; unsigned n; } ctx{&selectedKey, 0};
  world.visitBehaviours(
      [](void* c, Behaviour* b, GameObject* go, bool enabled) {
        Ctx& x = *static_cast<Ctx*>(c);
        if (!enabled) return;
        Gizmos::setColor(Color::white());
        Gizmos::setMatrix(Matrix4::identity());
        b->OnDrawGizmos();
        x.n++;
        if (!x.sel->empty() && playObjectKey(*go) == *x.sel) {
          Gizmos::setColor(Color::white());
          Gizmos::setMatrix(Matrix4::identity());
          b->OnDrawGizmosSelected();
          x.n++;
        }
      },
      &ctx);
  Gizmos::setColor(Color::white());
  Gizmos::setMatrix(Matrix4::identity());
  return ctx.n;
}

void EditModeWorld::drawGizmos(const std::string& selectedKey) {
  if (world_ && world_->valid()) drawGizmos(world_->world(), selectedKey);
}

}  // namespace editor
}  // namespace rensga
