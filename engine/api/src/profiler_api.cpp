// F6 (A3, API 12): Profiler::BeginSample / EndSample forward to the core sample table (rensga/core/script_samples.h). The runtime
// FrameProfiler prints the window as "rensga: prof sample ..." lines. A release build compiles RENSGA_PROFILE_SCOPE away; direct calls
// still work (they cost two clock reads).
#include "rensga/Profiler.h"
#include "rensga/core/script_samples.h"

namespace rensga {
void Profiler::BeginSample(const char* name) { script_samples::begin(name); }
void Profiler::EndSample() { script_samples::end(); }
u32 Profiler::LastSampleMicros(const char* name) { return script_samples::lastMicros(name); }
}  // namespace rensga
