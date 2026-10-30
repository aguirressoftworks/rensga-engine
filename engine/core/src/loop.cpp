#include "rensga/core/loop.h"

#include "rensga/core/components.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/scene_manager.h"
#include "rensga/core/input.h"
#include "rensga/core/time.h"

namespace rensga {

void GameLoop::init(World& world, const LoopConfig& config) {
  world_ = &world;
  config_ = config;
  accumulator_ = 0;
  frames_ = 0;
  fixedSteps_ = 0;
  TimeInternal::reset(config.fixedDelta);
}

bool GameLoop::frame(Scalar realDt, const VirtualGamepad& pad) {
  FrameHeapGuard::begin();
  if (realDt > config_.maxFrameTime) realDt = config_.maxFrameTime;
  Input::update(pad);
  TimeInternal::beginFrame(realDt);

  world_->startPending();
  // The accumulator runs on scaled time: timeScale 0.5 gives half the fixed steps per second.
  accumulator_ += Time::deltaTime();
  u32 steps = 0;
  while (accumulator_ >= config_.fixedDelta && steps < config_.maxStepsPerFrame) {
    TimeInternal::beginFixedStep(config_.fixedDelta);
    world_->fixedUpdate();
    if (hook_) hook_(*world_, config_.fixedDelta, hookUser_);
    Gameplay2D::fixedStep(*world_);
    accumulator_ -= config_.fixedDelta;
    ++steps;
    ++fixedSteps_;
  }
  if (steps == config_.maxStepsPerFrame && accumulator_ >= config_.fixedDelta) accumulator_ = 0;
  TimeInternal::setInterpolation(accumulator_ / config_.fixedDelta);
  UI::update(*world_);
  world_->update();
  world_->lateUpdate();
  world_->flushDestroyed();
  SceneManager::processPending(*world_);  // scene changes happen at the end of the frame
  ++frames_;
  heap::noteFrameDone();
  return FrameHeapGuard::end();
}

void GameLoop::run(IPlatform& platform, IRenderer* renderer, u32 maxFrames) {
  double last = platform.timeSeconds();
  VirtualGamepad pad;
  while (!platform.shouldQuit()) {
    double now = platform.timeSeconds();
    platform.pollInput(pad);
    frame(static_cast<Scalar>(now - last), pad);
    last = now;
    if (renderer) renderWorld(*world_, *renderer);
    if (maxFrames && frames_ >= maxFrames) break;
  }
}

static bool g_debugOverlay = false;
void setDebugOverlay(bool enabled) { g_debugOverlay = enabled; }
bool debugOverlayEnabled() { return g_debugOverlay; }

void drawMemoryOverlay(IRenderer& r, const MemoryBudget& budget, u32 refreshHz) {
  if (!g_debugOverlay) return;
  const i32 x = 8, barW = 120, barH = 6, gap = 4;
  const u32 n = budget.arenaCount();
  r.drawOverlayQuad(x - 4, 4, barW + 8, static_cast<i32>(n + 1) * (barH + gap) + 4, Color32(0, 0, 0, 160));
  i32 y = 8;
  for (u32 i = 0; i < n; ++i, y += barH + gap) {
    ArenaStats st;
    if (!budget.stats(i, st) || st.capacity == 0) continue;
    r.drawOverlayQuad(x, y, barW, barH, Color32(60, 60, 60, 255));
    i32 fill = static_cast<i32>(static_cast<u64>(st.used) * barW / st.capacity);
    i32 peak = static_cast<i32>(static_cast<u64>(st.peak) * barW / st.capacity);
    bool hot = st.used * 10 >= st.capacity * 9;  // 90% or more
    r.drawOverlayQuad(x, y, fill, barH, hot ? Color32(220, 60, 50, 255) : Color32(80, 170, 230, 255));
    if (peak > 0) r.drawOverlayQuad(x + (peak >= barW ? barW - 1 : peak), y, 1, barH, Color32(255, 255, 255, 255));
  }
  // Frame time: full bar = 2 refresh intervals.
  const Scalar target = 1.0f / static_cast<Scalar>(refreshHz ? refreshHz : 60);
  Scalar f = Time::unscaledDeltaTime() / (target * 2.0f);
  if (f > 1.0f) f = 1.0f;
  if (f < 0.0f) f = 0.0f;
  r.drawOverlayQuad(x, y, barW, barH, Color32(60, 60, 60, 255));
  bool over = Time::unscaledDeltaTime() > target * 1.1f;
  r.drawOverlayQuad(x, y, static_cast<i32>(f * static_cast<Scalar>(barW)), barH,
                    over ? Color32(220, 60, 50, 255) : Color32(90, 200, 90, 255));
  r.drawOverlayQuad(x + barW / 2, y, 1, barH, Color32(255, 255, 255, 255));  // one refresh interval
}

static OverlayHook g_overlayHook = nullptr;
void setOverlayHook(OverlayHook hook) { g_overlayHook = hook; }

void renderWorld(World& world, IRenderer& renderer, Color32 clear) {
  renderer.beginFrame(clear);
  Camera* cam = nullptr;
  world.forEach<Camera>([&](Camera& c) {
    if (!cam && c.gameObject() && c.gameObject()->activeInHierarchy()) cam = &c;
  });
  Matrix4 view;
  if (cam) {
    Scalar aspect = renderer.height() ? static_cast<Scalar>(renderer.width()) / static_cast<Scalar>(renderer.height()) : 1.0f;
    view = cam->transform()->localToWorld().inverse();
    renderer.setCamera(view, Matrix4::perspective(cam->fov, aspect, cam->nearClip, cam->farClip));
  }
  world.forEach<MeshRenderer>([&](MeshRenderer& mr) {
    GameObject* go = mr.gameObject();
    if (!go || mr.meshIndex == kInvalidIndex || !go->activeInHierarchy() || !mr.enabled()) return;
    Matrix4 w = go->transform.localToWorld();
    Scalar depth = -view.transformPoint(w.translationPart()).z;
    if (depth < 0) depth = 0;
    Scalar key = depth * 16.0f;
    u32 sortKey = key > 16777215.0f ? 16777215u : static_cast<u32>(key);
    renderer.drawMesh(mr.meshIndex, w, sortKey);
  });
  renderGameplay2D(world, renderer);  // sprites, tilemaps and UI draw after the 3D pass
  if (g_overlayHook) g_overlayHook(renderer);
  renderer.endFrame();
}

}  // namespace rensga
