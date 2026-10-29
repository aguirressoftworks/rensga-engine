// Time::deltaTime / Time::fixedDeltaTime (set by GameLoop).
#pragma once
#include "rensga/core/scalar.h"
#include "rensga/core/types.h"

namespace rensga {

struct Time {
  static inline Scalar deltaTime = 1.0f / 60.0f;       // seconds since the last frame (Update/LateUpdate)
  static inline Scalar fixedDeltaTime = 1.0f / 60.0f;  // fixed step (FixedUpdate)
  static inline Scalar time = 0;                       // seconds since start (frame time)
  static inline Scalar interpolation = 0;              // 0..1 leftover of the fixed step
  static inline u64 frameCount = 0;
};

}  // namespace rensga
