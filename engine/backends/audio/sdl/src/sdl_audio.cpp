#include "rensga/audio/sdl_audio.h"

#include <SDL.h>
#include <string.h>

#include "rensga/core/log.h"

namespace rensga {

bool SdlAudio::init(u32 sampleRate, AudioMixer* mixer) {
  shutdown();
  if (!mixer || sampleRate == 0) return true;  // nothing to play: behave like NullAudio
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    RLOG_ERROR("audio", "SDL audio init failed: %s", SDL_GetError());
    return false;
  }
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = static_cast<int>(sampleRate);
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = &SdlAudio::callback;
  want.userdata = this;
  const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);  // 0: SDL converts if needed
  if (dev == 0) {
    RLOG_ERROR("audio", "SDL_OpenAudioDevice failed: %s", SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return false;
  }
  mixer_ = mixer;
  rate_ = sampleRate;
  device_ = dev;
  write_.store(0);
  read_.store(0);
  underruns_.store(0);
  memset(ring_, 0, sizeof(ring_));
  update();  // prefill before the device starts pulling
  SDL_PauseAudioDevice(dev, 0);
  return true;
}

void SdlAudio::shutdown() {
  if (device_) {
    SDL_CloseAudioDevice(device_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
  device_ = 0;
  mixer_ = nullptr;
  rate_ = 0;
}

void SdlAudio::update() {
  if (!mixer_) return;
  mixer_->update();
  const u32 w = write_.load(std::memory_order_relaxed);
  const u32 r = read_.load(std::memory_order_acquire);
  const u32 buffered = w - r;
  if (buffered >= kTargetFrames) return;
  u32 need = kTargetFrames - buffered;
  u32 pos = w & (kRingFrames - 1);
  while (need > 0) {
    const u32 n = (kRingFrames - pos) < need ? (kRingFrames - pos) : need;
    mixer_->mix(ring_ + pos * 2, n);
    pos = (pos + n) & (kRingFrames - 1);
    need -= n;
  }
  write_.store(w + (kTargetFrames - buffered), std::memory_order_release);
}

void SdlAudio::pull(i16* out, u32 frames) {
  const u32 r = read_.load(std::memory_order_relaxed);
  const u32 w = write_.load(std::memory_order_acquire);
  u32 avail = w - r;
  u32 take = avail < frames ? avail : frames;
  u32 pos = r & (kRingFrames - 1);
  u32 done = 0;
  while (done < take) {
    const u32 n = (kRingFrames - pos) < (take - done) ? (kRingFrames - pos) : (take - done);
    memcpy(out + done * 2, ring_ + pos * 2, n * 2 * sizeof(i16));
    pos = (pos + n) & (kRingFrames - 1);
    done += n;
  }
  if (take < frames) {
    memset(out + take * 2, 0, (frames - take) * 2 * sizeof(i16));
    underruns_.fetch_add(1);
  }
  read_.store(r + take, std::memory_order_release);
}

void SdlAudio::callback(void* user, unsigned char* stream, int len) {
  static_cast<SdlAudio*>(user)->pull(reinterpret_cast<i16*>(stream), static_cast<u32>(len) / (2 * sizeof(i16)));
}

}  // namespace rensga
