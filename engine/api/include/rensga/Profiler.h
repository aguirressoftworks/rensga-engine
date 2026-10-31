// F6 contract (C0): script profiler markers. Names must be string literals (static storage); the profiler keeps the pointer.
//
//   void Update() { Profiler::BeginSample("AI"); think(); Profiler::EndSample(); }     // or RENSGA_PROFILE_SCOPE("AI");
//
// Samples nest up to kMaxDepth. In a development build they feed the "rensga: prof" channel and the editor Profiler window (D2,
// Stage 3); in a release build they compile to nothing. Stub until A3/D2: the calls do nothing. Defined in
// engine/api/src/scene_api.cpp.
#pragma once
#include "rensga/core/types.h"

namespace rensga {
namespace Profiler {
constexpr u32 kMaxDepth = 8;
constexpr u32 kMaxSamples = 32;  // distinct names per frame
void BeginSample(const char* name);
void EndSample();
// Microseconds the last full frame spent in `name` (0 if unknown); for in-game debug overlays.
u32 LastSampleMicros(const char* name);
}  // namespace Profiler

struct ProfileScope {
  explicit ProfileScope(const char* name) { Profiler::BeginSample(name); }
  ~ProfileScope() { Profiler::EndSample(); }
  ProfileScope(const ProfileScope&) = delete;
  ProfileScope& operator=(const ProfileScope&) = delete;
};

}  // namespace rensga

#if defined(RENSGA_RELEASE_BUILD)
#define RENSGA_PROFILE_SCOPE(name) ((void)0)
#else
#define RENSGA_PROFILE_CONCAT2(a, b) a##b
#define RENSGA_PROFILE_CONCAT(a, b) RENSGA_PROFILE_CONCAT2(a, b)
#define RENSGA_PROFILE_SCOPE(name) ::rensga::ProfileScope RENSGA_PROFILE_CONCAT(profileScope_, __LINE__)(name)
#endif
