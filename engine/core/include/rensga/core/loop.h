// MVP-11: fixed-timestep loop (refreshHz, 60 by default) with an interpolation factor.
// Each frame: startPending (Awake/OnEnable/Start), then FixedUpdate xN (+ physics hook), Update,
// LateUpdate, destroy flush. The heap guard wraps the whole frame.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/world.h"

namespace rensga {

struct LoopConfig {
  u32 refreshHz = 60;           // video refresh: 60 NTSC, 50 PAL (ConsoleTarget kRefreshHz)
  Scalar fixedDelta = 1.0f / 60.0f;  // fixed step; forRefreshHz() sets it to 1 / refreshHz
  Scalar maxFrameTime = 0.25f;  // clamps spikes so the accumulator cannot spiral
  u32 maxStepsPerFrame = 8;

  static LoopConfig forRefreshHz(u32 hz) {
    LoopConfig c;
    c.refreshHz = hz ? hz : 60;
    c.fixedDelta = 1.0f / static_cast<Scalar>(c.refreshHz);
    return c;
  }
};

class GameLoop {
 public:
  // Optional step run after every FixedUpdate round (physics: CharacterController, triggers).
  using FixedStepHook = void (*)(World& world, Scalar fixedDelta, void* user);

  void init(World& world, const LoopConfig& config = LoopConfig());
  void setFixedStepHook(FixedStepHook hook, void* user) { hook_ = hook; hookUser_ = user; }

  // Advance by `realDt` seconds with this frame's polled pad. Returns false if the heap guard
  // failed the frame.
  bool frame(Scalar realDt, const VirtualGamepad& pad);

  // Runs frames until the platform asks to quit (or maxFrames > 0 frames passed), then draws
  // through `renderer` if non-null. A world camera + MeshRenderers are submitted each frame.
  void run(IPlatform& platform, IRenderer* renderer, u32 maxFrames = 0);

  // Same value as Time::interpolation(): leftover of the fixed step, 0..1.
  Scalar interpolation() const { return accumulator_ / config_.fixedDelta; }
  const LoopConfig& config() const { return config_; }
  u64 frameCount() const { return frames_; }
  u64 fixedStepCount() const { return fixedSteps_; }

 private:
  World* world_ = nullptr;
  LoopConfig config_;
  Scalar accumulator_ = 0;
  u64 frames_ = 0;
  u64 fixedSteps_ = 0;
  FixedStepHook hook_ = nullptr;
  void* hookUser_ = nullptr;
};

// Optional callback run by renderWorld after all meshes, before endFrame (HUD overlay quads).
using OverlayHook = void (*)(IRenderer& renderer);
void setOverlayHook(OverlayHook hook);

// Debug overlay: one bar per MemoryBudget arena (fill = used, tick = peak) and a frame-time bar
// (full width = 2x the refresh interval; green within budget, red over). Drawn with
// drawOverlayQuad only; text labels come with the 2D renderer. Does nothing unless enabled.
void setDebugOverlay(bool enabled);
bool debugOverlayEnabled();
void drawMemoryOverlay(IRenderer& renderer, const MemoryBudget& budget, u32 refreshHz = 60);

// Submits the first Camera and every MeshRenderer of `world` to the renderer (one frame).
// sortKey = view-space distance in 1/16 units, clamped (front to back).
void renderWorld(World& world, IRenderer& renderer, Color32 clear = Color32(40, 60, 90, 255));

}  // namespace rensga
