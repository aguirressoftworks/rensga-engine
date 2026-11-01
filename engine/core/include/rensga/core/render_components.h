// F6 (C0 contract, B2 behaviour, API 7): ParticleSystem and LineRenderer. The methods are defined in engine/api/src/render_api.cpp,
// the simulation and drawing in engine/core/src/{particles,line_renderer}.cpp (render_view.h).
// CPU particles with a fixed pool block per system (maxParticles, no heap); the random stream is seeded from `seed` so the
// simulation is deterministic. Shapes emit along the object's +Y (Cone, Hemisphere, Box, Circle) or outward (Sphere, Point).
#pragma once
#include "rensga/core/object.h"

namespace rensga {

struct ParticleSystem : Component {
  REFLECT(ParticleSystem)
  HEADER("Main")
  SERIALIZE_FIELD(Scalar, duration, 5.0f)
  SERIALIZE_FIELD(bool, loop, true)
  TOOLTIP("Start playing when the object wakes up.")
  SERIALIZE_FIELD(bool, playOnAwake, true)
  SERIALIZE_FIELD(Scalar, startLifetime, 1.0f)
  SERIALIZE_FIELD(Scalar, startSpeed, 5.0f)
  SERIALIZE_FIELD(Scalar, startSize, 1.0f)
  SERIALIZE_FIELD(Color32, startColor, Color32(255, 255, 255, 255))
  TOOLTIP("Multiplies the world gravity (0 = none).")
  SERIALIZE_FIELD(Scalar, gravityModifier, 0.0f)
  TOOLTIP("Pool size of this system. The profile caps the sum over all systems.")
  RANGE(1, 512)
  SERIALIZE_FIELD(i32, maxParticles, 64)
  TOOLTIP("Seed of the random stream (same seed, same particles).")
  SERIALIZE_FIELD(i32, seed, 1)
  HEADER("Emission")
  TOOLTIP("Particles per second.")
  SERIALIZE_FIELD(Scalar, emissionRate, 10.0f)
  TOOLTIP("Bursts: time (seconds from the start) and particle count, pairwise.")
  SERIALIZE_ARRAY(Scalar, burstTimes, 4)
  SERIALIZE_ARRAY(i32, burstCounts, 4)
  HEADER("Shape")
  SERIALIZE_ENUM(shape, 0, "Cone|Sphere|Hemisphere|Box|Circle|Point")
  SERIALIZE_FIELD(Scalar, shapeRadius, 1.0f)
  TOOLTIP("Cone opening angle in degrees.")
  RANGE(0, 90)
  SERIALIZE_FIELD(Scalar, shapeAngle, 25.0f)
  HEADER("Over lifetime")
  SERIALIZE_FIELD(bool, colorOverLifetime, false)
  SERIALIZE_FIELD(Color32, colorStart, Color32(255, 255, 255, 255))
  SERIALIZE_FIELD(Color32, colorEnd, Color32(255, 255, 255, 0))
  SERIALIZE_FIELD(bool, sizeOverLifetime, false)
  TOOLTIP("Multipliers of startSize at birth and at death.")
  SERIALIZE_FIELD(Scalar, sizeStart, 1.0f)
  SERIALIZE_FIELD(Scalar, sizeEnd, 0.0f)
  HEADER("Renderer")
  SERIALIZE_FIELD(Guid, texture, Guid())

  // Runtime state (not reflected), owned by the particle system (B2).
  u32 poolIndex_ = kInvalidIndex;
  bool playing_ = false;
  u32 liveCount_ = 0;

  // ---- Unity API. Defined in engine/api/src/render_api.cpp (stubs until B2).
  void Play();
  void Stop();                 // stops emitting; living particles finish
  void Emit(i32 count);        // emits `count` particles now (capped by maxParticles)
  void Clear();                // removes every particle
  bool isPlaying() const { return playing_; }
  u32 particleCount() const { return liveCount_; }
};

// A polyline of up to 16 points drawn as camera-facing quads of `width`.
struct LineRenderer : Component {
  REFLECT(LineRenderer)
  SERIALIZE_ARRAY(Vector3, positions, 16)
  SERIALIZE_FIELD(Scalar, width, 0.1f)
  SERIALIZE_FIELD(Color32, color, Color32(255, 255, 255, 255))
  SERIALIZE_FIELD(Guid, texture, Guid())
  TOOLTIP("Positions are world space (true) or relative to the object (false).")
  SERIALIZE_FIELD(bool, useWorldSpace, true)
  SERIALIZE_FIELD(bool, loop, false)

  u32 positionCount() const { return positions.count; }
  void SetPositionCount(u32 n) { positions.count = n < decltype(positions)::kCapacity ? n : decltype(positions)::kCapacity; }
  void SetPosition(u32 index, const Vector3& p) {
    if (index < decltype(positions)::kCapacity) {
      positions.items[index] = p;
      if (index >= positions.count) positions.count = index + 1;
    }
  }
  Vector3 GetPosition(u32 index) const { return index < positions.count ? positions.items[index] : Vector3(); }
};

}  // namespace rensga
