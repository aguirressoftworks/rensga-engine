// MVP-11: fixed-timestep loop at 60 Hz with an interpolation factor.
// Each frame: startPending (Awake/OnEnable/Start), then FixedUpdate xN (+ physics hook), Update,
// LateUpdate, destroy flush. The heap guard wraps the whole frame.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/world.h"

namespace rensga {

struct LoopConfig {
  Scalar fixedDelta = 1.0f / 60.0f;
  Scalar maxFrameTime = 0.25f;  // clamps spikes so the accumulator cannot spiral
  u32 maxStepsPerFrame = 8;
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

  Scalar interpolation() const { return accumulator_ / config_.fixedDelta; }
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

// Submits the first Camera and every MeshRenderer of `world` to the renderer (one frame).
// sortKey = view-space distance in 1/16 units, clamped (front to back).
void renderWorld(World& world, IRenderer& renderer, Color32 clear = Color32(40, 60, 90, 255));

}  // namespace rensga
