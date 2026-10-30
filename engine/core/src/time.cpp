#include "rensga/core/time.h"

namespace rensga {

namespace {
struct State {
  Scalar delta = 1.0f / 60.0f, fixedDelta = 1.0f / 60.0f, time = 0, fixedTime = 0;
  Scalar unscaledDelta = 1.0f / 60.0f, unscaledTime = 0, scale = 1.0f, interp = 0;
  u64 frames = 0;
};
State g;
}  // namespace

Scalar Time::deltaTime() { return g.delta; }
Scalar Time::fixedDeltaTime() { return g.fixedDelta; }
Scalar Time::time() { return g.time; }
Scalar Time::fixedTime() { return g.fixedTime; }
Scalar Time::unscaledDeltaTime() { return g.unscaledDelta; }
Scalar Time::unscaledTime() { return g.unscaledTime; }
Scalar Time::timeScale() { return g.scale; }
void Time::setTimeScale(Scalar scale) { g.scale = scale < Scalar(0) ? Scalar(0) : scale; }
u64 Time::frameCount() { return g.frames; }
Scalar Time::interpolation() { return g.interp; }

namespace TimeInternal {
void reset(Scalar fixedDelta) {
  Scalar keep = g.scale;
  g = State();
  g.scale = keep;
  g.fixedDelta = fixedDelta;
}
void beginFrame(Scalar unscaledDt) {
  g.unscaledDelta = unscaledDt;
  g.unscaledTime += unscaledDt;
  g.delta = unscaledDt * g.scale;
  g.time += g.delta;
  ++g.frames;
}
void beginFixedStep(Scalar fixedDelta) {
  g.fixedDelta = fixedDelta;
  g.fixedTime += fixedDelta;
}
void setInterpolation(Scalar t) { g.interp = t; }
}  // namespace TimeInternal

}  // namespace rensga
