// SDL2 audio backend (PC, Android). The device callback only copies from a lock-free
// single-producer/single-consumer ring; the main thread mixes into the ring from update().
// So the mixer stays single threaded and the audio thread never touches files or the mixer.
// Works with SDL_AUDIODRIVER=disk or dummy (tests, CI).
#pragma once
#include <atomic>

#include "rensga/core/audio.h"
#include "rensga/core/backends.h"

namespace rensga {

class SdlAudio : public IAudioBackend {
 public:
  static constexpr u32 kRingFrames = 8192;     // power of two
  static constexpr u32 kTargetFrames = 3072;   // update() keeps about this much buffered
  ~SdlAudio() override { shutdown(); }
  bool init(u32 sampleRate = 22050, AudioMixer* mixer = nullptr) override;
  void shutdown() override;
  void update() override;
  u32 sampleRate() const override { return rate_; }
  u32 underruns() const { return underruns_.load(); }

 private:
  static void callback(void* user, unsigned char* stream, int len);
  void pull(i16* out, u32 frames);

  AudioMixer* mixer_ = nullptr;
  unsigned device_ = 0;
  u32 rate_ = 0;
  alignas(16) i16 ring_[kRingFrames * 2];
  std::atomic<u32> write_{0}, read_{0};  // frame counters, free running
  std::atomic<u32> underruns_{0};
};

}  // namespace rensga
