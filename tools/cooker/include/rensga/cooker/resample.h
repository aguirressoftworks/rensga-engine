// F5.17: cook-time audio resampling to the target rate. HOST ONLY.
#pragma once
#include <cstdint>
#include <string>

#include "rensga/cooker/assets.h"

namespace rensga {

// Windowed-sinc (Blackman-Harris, 16 zero crossings) resampler. Only ever lowers the rate (a clip below the target keeps its rate; the
// mixer interpolates at play time). The cutoff is the new Nyquist, so nothing above it aliases. Deterministic: plain double math, no
// threads. Edges repeat the first and last frame. Keeps channel count; a result that would be empty leaves the clip untouched.
void audioResampleSinc(PcmAudio& pcm, uint32_t newRate);

// The import settings JSON text with "sampleRate": targetRate added when the asset did not set one. "" in -> a one-key object. Leaves
// the text alone when targetRate is 0 or the asset sets its own rate. Used so the per-asset cook cache key follows the target rate.
std::string withTargetAudioRate(const std::string& importSettings, uint32_t targetRate);

}  // namespace rensga
