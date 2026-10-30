// API 3: Unity-style Time getters. The game loop owns the values through TimeInternal.
// deltaTime/time are scaled by timeScale, unscaled* are not. fixedTime advances one
// fixedDeltaTime per FixedUpdate step.
#pragma once
#include "rensga/core/scalar.h"
#include "rensga/core/types.h"

namespace rensga {

struct Time {
  static Scalar deltaTime();          // seconds since the last frame (scaled)
  static Scalar fixedDeltaTime();     // fixed step (FixedUpdate)
  static Scalar time();               // seconds since start (scaled, frame time)
  static Scalar fixedTime();          // seconds of fixed steps run so far
  static Scalar unscaledDeltaTime();
  static Scalar unscaledTime();
  static Scalar timeScale();
  static void setTimeScale(Scalar scale);  // clamped to >= 0
  static u64 frameCount();
  static Scalar interpolation();      // 0..1 leftover of the fixed step, for renderers
};

// Written by GameLoop only.
namespace TimeInternal {
void reset(Scalar fixedDelta);              // start of a run; keeps timeScale
void beginFrame(Scalar unscaledDt);         // sets delta/time (applies timeScale), counts the frame
void beginFixedStep(Scalar fixedDelta);     // sets fixedDeltaTime, advances fixedTime
void setInterpolation(Scalar t);
}  // namespace TimeInternal

}  // namespace rensga
