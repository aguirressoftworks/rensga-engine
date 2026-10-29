// Headless simulation helpers (host sim + tests): scripted VirtualGamepad input, a deterministic
// frame driver over the real GameLoop, and the state hash. Used by rensga_headless.
#pragma once
#include "rensga/Rensga.h"

namespace rensga {

// Piecewise-constant gamepad script: the last step with frame <= current frame applies.
class InputScript {
 public:
  static constexpr u32 kMaxSteps = 256;
  struct Step { u32 frame; Scalar x, y; u32 buttons; };
  enum Button : u32 { South = 1, East = 2, West = 4, North = 8, L = 16, R = 32, Start = 64, Select = 128,
                      Up = 256, Down = 512, Left = 1024, Right = 2048 };
  bool add(u32 frame, Scalar x, Scalar y, u32 buttons);
  // Parses {"steps":[{"frame":0,"x":0,"y":1,"buttons":["south","l"]}, ...]} (steps in frame order).
  bool parse(const char* text, size_t size);
  void apply(u32 frame, VirtualGamepad& pad) const;
  u32 count() const { return count_; }

 private:
  Step steps_[kMaxSteps];
  u32 count_ = 0;
};

// Deterministic FNV-1a 32 over every live object's world position/rotation bytes plus the number
// of Pickup components that are gone (collected).
u32 stateHash(World& world, u32 initialPickups);
// Number of live components of the reflected type `typeName` (0 if unknown).
u32 liveCount(World& world, const char* typeName);

struct SimResult {
  u32 hash = 0;
  u32 frames = 0;
};
// Runs `frames` frames of the real loop (fixed 1/60 s real time, physics hook installed), logging
// "rensga: frame <n>" every 60 frames. `input` may be null.
SimResult runSim(World& world, u32 frames, const InputScript* input, u32 initialPickups);

// Registers the template InputActions (Move/Jump/CameraLeft/CameraRight) in code.
void defineDefaultInput();
// Builds the TestLevel programmatically (same objects/components/scripts as TestLevel.scene).
// Returns false if a script type is not linked in.
bool buildTestLevel(World& world);

}  // namespace rensga
