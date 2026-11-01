#include "rensga/core/loop.h"
#include "rensga/core/application.h"
#include "rensga/core/animation.h"
#include "rensga/core/cull.h"
#include "rensga/core/lighting.h"
#include "rensga/core/render_view.h"
#include "rensga/core/app_events.h"
#include "rensga/core/dev_control.h"
#include "rensga/core/system.h"

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
  Input::update(pad, realDt);
  TimeInternal::beginFrame(realDt);
  world_->scheduler().beginFrame();

  world_->startPending();
  // The accumulator runs on scaled time: timeScale 0.5 gives half the fixed steps per second.
  accumulator_ += Time::deltaTime();
  u32 steps = 0;
  while (accumulator_ >= config_.fixedDelta && steps < config_.maxStepsPerFrame) {
    TimeInternal::beginFixedStep(config_.fixedDelta);
    world_->fixedUpdate();
    if (hook_) hook_(*world_, config_.fixedDelta, hookUser_);
    Gameplay2D::fixedStep(*world_);
    AnimationSystem::fixedStep(*world_);  // F5: skeletal clips advance on the fixed step (integer ticks)
    Particles::fixedStep(*world_, config_.fixedDelta);  // F6.09 (B2): visual only, nothing in the simulation reads it
    world_->scheduler().runFixed();  // WaitForFixedUpdate resumes after the fixed step
    accumulator_ -= config_.fixedDelta;
    ++steps;
    ++fixedSteps_;
  }
  if (steps == config_.maxStepsPerFrame && accumulator_ >= config_.fixedDelta) accumulator_ = 0;
  TimeInternal::setInterpolation(accumulator_ / config_.fixedDelta);
  UI::update(*world_);
  world_->scheduler().runInvokes();  // Invoke / InvokeRepeating timers fire before Update
  world_->update();
  world_->scheduler().runUpdate();   // coroutines: next frame, WaitForSeconds, WaitUntil, WaitWhile
  world_->lateUpdate();
  world_->scheduler().runEndOfFrame();  // WaitForEndOfFrame
  world_->flushDestroyed();
  SceneManager::processPending(*world_);  // scene changes happen at the end of the frame
  ++frames_;
  Input::flushVibration();
  heap::noteFrameDone();
  return FrameHeapGuard::end();
}

void GameLoop::run(IPlatform& platform, IRenderer* renderer, u32 maxFrames) {
  system_services::bind(&platform);  // Application::SetCpuClock, TouchScreenKeyboard, SystemInfo talk to this platform
  double last = platform.timeSeconds();
  VirtualGamepad pad;
  while (!platform.shouldQuit() && !Application::quitRequested()) {
    double now = platform.timeSeconds();
    platform.pollInput(pad);
    pumpApplicationEvents(*world_);  // suspend / Home -> OnApplicationPause
    const u32 toRun = devControlPoll();  // 1 = normal, 0 = paused by the editor, N = step N fixed frames
    if (toRun == 1) {
      frame(static_cast<Scalar>(now - last), pad);
    } else {
      for (u32 i = 0; i < toRun; ++i) frame(config_.fixedDelta, pad);
    }
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

namespace {
Color32 color32FromABGR(u32 v) {
  return Color32(static_cast<u8>(v & 255), static_cast<u8>((v >> 8) & 255), static_cast<u8>((v >> 16) & 255), static_cast<u8>(v >> 24));
}
u8 mulU8(u8 a, u8 b) { return static_cast<u8>((static_cast<u32>(a) * b + 127) / 255); }
Scalar maxScale(const Matrix4& w) {
  Vector3 s = w.lossyScale();
  Scalar m = s.x > s.y ? s.x : s.y;
  return m > s.z ? m : s.z;
}
}  // namespace

// F6.09 (B2): cameras. Every enabled, active Camera draws in increasing depth (ties: creation order) into its viewport rectangle,
// clears as its clearFlags say and draws the objects whose layer is in its cullingMask. A scene with one default Camera makes the same
// renderer calls as before F6: beginFrame, setCamera, lights, draws.
void renderWorld(World& world, IRenderer& renderer, Color32 clear) {
  const u32 screenW = renderer.width(), screenH = renderer.height();
  RenderScreen::set(screenW, screenH);
  constexpr u32 kMaxCameras = 8;
  Camera* cams[kMaxCameras];
  u32 camCount = 0;
  world.forEach<Camera>([&](Camera& c) {
    if (!c.gameObject() || !c.enabled() || !c.gameObject()->activeInHierarchy() || camCount >= kMaxCameras) return;
    u32 j = camCount++;
    while (j > 0 && cams[j - 1]->depth > c.depth) { cams[j] = cams[j - 1]; --j; }
    cams[j] = &c;
  });
  const Color32 kDefaultBackground(40, 60, 90, 255);
  auto backgroundOf = [&](const Camera& c) { return c.backgroundColor == kDefaultBackground ? clear : c.backgroundColor; };
  renderer.beginFrame(camCount ? backgroundOf(*cams[0]) : clear);

  // F5: the shared-3D tables (MSHL parallel to MESH). Without them every mesh takes the baked drawMesh path below.
  const PackMeshLit* lit = nullptr;
  u32 litCount = 0, maxLights = 4;
  const PackAssets* assets = nullptr;
  if (GfxAssets::bound()) {
    if (const PackView* pv = GfxAssets::pack()) {
      lit = pv->records<PackMeshLit>(pack_section::MSHL, &litCount);
      maxLights = resolveMaxLights(pv->consoleTargetExt(), Application::platform());
    }
    assets = &GfxAssets::assets();
  }
  LightSet lights;
  gatherLights(world, lights);

  bool viewportChanged = false;
  const u32 passes = camCount ? camCount : 1;  // no Camera: one pass without setCamera (the meshes are dropped by the renderers)
  for (u32 pass = 0; pass < passes; ++pass) {
    Camera* cam = camCount ? cams[pass] : nullptr;
    Matrix4 view;
    Frustum frustum;  // invalid = never cull
    CameraView cv;
    i32 cullMask = -1;
    if (cam) {
      cv = makeCameraView(*cam, screenW, screenH);
      if (cv.w <= 0 || cv.h <= 0) continue;
      cullMask = cam->cullingMask;
      if (!cv.full) {
        renderer.setViewport(cv.x, cv.y, cv.w, cv.h);
        viewportChanged = true;
      } else if (viewportChanged) {
        renderer.setViewport(0, 0, static_cast<i32>(screenW), static_cast<i32>(screenH));
        viewportChanged = false;
      }
      if (pass > 0) {  // the first camera was cleared by beginFrame
        switch (static_cast<CameraClear>(cam->clearFlags)) {
          case CameraClear::Skybox:
          case CameraClear::SolidColor: renderer.clearView(kClearColor | kClearDepth, backgroundOf(*cam)); break;
          case CameraClear::DepthOnly: renderer.clearView(kClearDepth, backgroundOf(*cam)); break;
          case CameraClear::Nothing: break;
        }
      }
      view = cv.view;
      renderer.setCamera(cv.view, cv.proj);
      frustum = frustumFromViewProj(cv.proj * cv.view);
    }
    if (pass == 0 && (lit || lights.fogOn)) {  // a pack of only baked meshes (MSHL absent, no fog) sees exactly the old call sequence
      renderer.setFrameLights(lights.lights, lights.count, lights.ambient);
      renderer.setFog(lights.fogOn, lights.fogColor, lights.fogStart, lights.fogEnd);
    }

    // One draw of a mesh that has an MSHL record. `matGuid` overrides the record's material when it names a MATL entry; `block`
    // is the renderer's MaterialPropertyBlock (per-instance color and texture, the material stays shared).
    auto drawLit = [&](u32 meshIndex, const Guid& matGuid, Color32 tint, const Matrix4& w, u32 sortKey, const SkinnedMeshRenderer* smr,
                       const MaterialPropertyBlock* block) {
      const PackMeshLit& m = lit[meshIndex];
      const Vector3 center = w.transformPoint(Vector3(m.boundsCenter[0], m.boundsCenter[1], m.boundsCenter[2]));
      const Scalar radius = m.boundsRadius * maxScale(w);
      if (m.boundsRadius > 0 && !sphereInFrustum(frustum, center, radius)) return;
      DrawParams dp;
      const PackMaterial* mat = nullptr;
      if (assets) {
        mat = assets->findMaterial(matGuid);
        if (!mat && m.material != kInvalidIndex && m.material < assets->materialCount) mat = &assets->materials[m.material];
      }
      dp.matColor = tint;
      if (mat) {
        if (mat->texture != kInvalidIndex) dp.texture = GfxAssets::handle(mat->texture);
        Color32 mc = color32FromABGR(mat->colorABGR);
        dp.matColor = Color32(mulU8(mc.r, tint.r), mulU8(mc.g, tint.g), mulU8(mc.b, tint.b), mulU8(mc.a, tint.a));
        dp.matFlags = mat->flags;
      }
      if (block) {
        if (block->hasColor) {
          const Color32 bc = block->color;
          dp.matColor = Color32(mulU8(dp.matColor.r, bc.r), mulU8(dp.matColor.g, bc.g), mulU8(dp.matColor.b, bc.b), mulU8(dp.matColor.a, bc.a));
        }
        if (block->hasTexture) {
          const u32 ti = GfxAssets::textureIndex(block->texture);
          if (ti != kInvalidIndex) dp.texture = GfxAssets::handle(ti);
        }
      }
      if (m.flags & kMeshLightBaked) dp.matFlags |= kMatUnlit;  // F6 b3: baked vertex lighting, no run-time lights
      if (!(dp.matFlags & kMatUnlit)) {
        u8 idx[kMaxDrawLights];
        dp.lightCount = static_cast<u8>(selectLights(lights, center, radius, maxLights, idx));
        for (u32 i = 0; i < dp.lightCount; ++i) dp.lights[i] = idx[i];
      }
      if (smr && m.isSkinned()) dp.palette = AnimationSystem::palette(world, *smr, &dp.paletteCount);
      renderer.drawMeshEx(meshIndex, w, sortKey, dp);
    };
    auto sortKeyOf = [&](const Matrix4& w) {
      Scalar depth = -view.transformPoint(w.translationPart()).z;
      if (depth < 0) depth = 0;
      Scalar key = depth * 16.0f;
      return key > 16777215.0f ? 16777215u : static_cast<u32>(key);
    };

    world.forEach<MeshRenderer>([&](MeshRenderer& mr) {
      GameObject* go = mr.gameObject();
      if (!go || mr.meshIndex == kInvalidIndex || !go->activeInHierarchy() || !mr.enabled()) return;
      if (cullMask != -1 && !layerInMask(cullMask, go->layer)) return;
      Matrix4 w = go->transform.localToWorld();
      const u32 sortKey = sortKeyOf(w);
      if (lit && mr.meshIndex < litCount && !lit[mr.meshIndex].isBaked())
        drawLit(mr.meshIndex, mr.material, mr.color, w, sortKey, nullptr, mr.propertyBlock_.isEmpty() ? nullptr : &mr.propertyBlock_);
      else renderer.drawMesh(mr.meshIndex, w, sortKey);
    });
    world.forEach<SkinnedMeshRenderer>([&](SkinnedMeshRenderer& sr) {
      GameObject* go = sr.gameObject();
      if (!go || sr.meshIndex == kInvalidIndex || !go->activeInHierarchy() || !sr.enabled()) return;
      if (cullMask != -1 && !layerInMask(cullMask, go->layer)) return;
      Matrix4 w = go->transform.localToWorld();
      const u32 sortKey = sortKeyOf(w);
      if (lit && sr.meshIndex < litCount && !lit[sr.meshIndex].isBaked())
        drawLit(sr.meshIndex, sr.material, Color32(255, 255, 255, 255), w, sortKey, &sr, sr.propertyBlock_.isEmpty() ? nullptr : &sr.propertyBlock_);
      else renderer.drawMesh(sr.meshIndex, w, sortKey);
    });
    if (cam) {  // F6.09: effects after the opaque meshes of this camera, back to front, blended, not written to the depth buffer
      Particles::draw(world, renderer, cv, cullMask);
      Ribbons::drawLines(world, renderer, cv, cullMask);
      Ribbons::drawDebug(renderer, cv);
    }
  }
  if (viewportChanged) renderer.setViewport(0, 0, static_cast<i32>(screenW), static_cast<i32>(screenH));
  renderGameplay2D(world, renderer);  // sprites, tilemaps and UI draw after the 3D pass
  if (g_overlayHook) g_overlayHook(renderer);
  renderer.endFrame();
}

}  // namespace rensga
