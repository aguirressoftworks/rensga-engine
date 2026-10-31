// F5.17: windowed-sinc resampler and the target-rate import setting. See rensga/cooker/resample.h.
#include "rensga/cooker/resample.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <nlohmann/json.hpp>

namespace rensga {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kZeroCrossings = 16;

double blackmanHarris(double t) {  // t in [-1, 1]
  if (t <= -1.0 || t >= 1.0) return 0.0;
  const double x = kPi * (t + 1.0);  // 0 .. 2pi
  return 0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x);
}
double sinc(double x) { return std::fabs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x); }
}  // namespace

void audioResampleSinc(PcmAudio& pcm, uint32_t newRate) {
  if (!newRate || !pcm.sampleRate || newRate >= pcm.sampleRate || !pcm.channels) return;
  const uint32_t ch = pcm.channels, inFrames = pcm.frames();
  const uint64_t outFrames = static_cast<uint64_t>(inFrames) * newRate / pcm.sampleRate;
  if (!outFrames) return;
  const double ratio = static_cast<double>(pcm.sampleRate) / newRate;  // > 1: input frames per output frame
  const double half = kZeroCrossings * ratio;                          // kernel half width in input frames
  const int64_t reach = static_cast<int64_t>(std::ceil(half));
  std::vector<int16_t> o(static_cast<size_t>(outFrames) * ch);
  std::vector<double> w(static_cast<size_t>(2 * reach + 2));
  for (uint64_t f = 0; f < outFrames; ++f) {
    const double pos = static_cast<double>(f) * ratio;
    const int64_t base = static_cast<int64_t>(std::floor(pos));
    double sum = 0;
    size_t n = 0;
    for (int64_t k = base - reach + 1; k <= base + reach; ++k, ++n) {
      const double d = static_cast<double>(k) - pos;
      w[n] = sinc(d / ratio) * blackmanHarris(d / half);
      sum += w[n];
    }
    const double norm = sum != 0.0 ? 1.0 / sum : 1.0;  // unity gain at DC, whatever the fractional position
    for (uint32_t c = 0; c < ch; ++c) {
      double acc = 0;
      n = 0;
      for (int64_t k = base - reach + 1; k <= base + reach; ++k, ++n) {
        const int64_t i = std::min<int64_t>(std::max<int64_t>(k, 0), inFrames - 1);
        acc += w[n] * pcm.samples[static_cast<size_t>(i) * ch + c];
      }
      const long v = std::lround(acc * norm);
      o[f * ch + c] = static_cast<int16_t>(std::min<long>(32767, std::max<long>(-32768, v)));
    }
  }
  pcm.samples.swap(o);
  pcm.sampleRate = newRate;
}

std::string withTargetAudioRate(const std::string& importSettings, uint32_t targetRate) {
  if (!targetRate) return importSettings;
  nlohmann::json j = nlohmann::json::object();
  if (!importSettings.empty()) {
    j = nlohmann::json::parse(importSettings, nullptr, false);
    if (!j.is_object()) return importSettings;  // a bad file: parseAudioImport reports it
  }
  if (j.contains("sampleRate") && j["sampleRate"].is_number() && j["sampleRate"].get<uint32_t>() != 0) return importSettings;
  j["sampleRate"] = targetRate;
  return j.dump();
}

}  // namespace rensga
