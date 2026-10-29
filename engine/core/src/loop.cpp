#include "rensga/core/loop.h"

#include "rensga/core/components.h"
#include "rensga/core/input.h"
#include "rensga/core/time.h"

namespace rensga {

void GameLoop::init(World& world, const LoopConfig& config) {
  world_ = &world;
  config_ = config;
  accumulator_ = 0;
  frames_ = 0;
  fixedSteps_ = 0;
  Time::fixedDeltaTime = config.fixedDelta;
  Time::frameCount = 0;
  Time::time = 0;
}

bool GameLoop::frame(Scalar realDt, const VirtualGamepad& pad) {
  FrameHeapGuard::begin();
  if (realDt > config_.maxFrameTime) realDt = config_.maxFrameTime;
  Input::update(pad);
  Time::deltaTime = realDt;
  Time::time += realDt;

  world_->startPending();
  accumulator_ += realDt;
  u32 steps = 0;
  while (accumulator_ >= config_.fixedDelta && steps < config_.maxStepsPerFrame) {
    Time::fixedDeltaTime = config_.fixedDelta;
    world_->fixedUpdate();
    if (hook_) hook_(*world_, config_.fixedDelta, hookUser_);
    accumulator_ -= config_.fixedDelta;
    ++steps;
    ++fixedSteps_;
  }
  if (steps == config_.maxStepsPerFrame && accumulator_ >= config_.fixedDelta) accumulator_ = 0;
  Time::interpolation = accumulator_ / config_.fixedDelta;
  world_->update();
  world_->lateUpdate();
  world_->flushDestroyed();
  ++frames_;
  ++Time::frameCount;
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
  renderer.endFrame();
}

}  // namespace rensga
