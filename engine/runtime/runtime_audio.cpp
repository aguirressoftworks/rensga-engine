#include "runtime_audio.h"

#include <string.h>

#include "rensga/Audio.h"
#include "rensga/core/log.h"
#if defined(RENSGA_PLATFORM_PS2)
#include "rensga/audio/ps2_audio.h"
#else
#include "rensga/audio/sdl_audio.h"
#endif

namespace rensga {
namespace {
#if defined(RENSGA_PLATFORM_PS2)
Ps2Audio g_backend;
#else
SdlAudio g_backend;
#endif

// Built-in tones for --audio-stress when the pack has no clips: 4 square waves, 0.5 s each (static, no heap).
constexpr u32 kToneFrames = 11025;
constexpr u32 kToneCount = 4;
i16 g_tones[kToneCount][kToneFrames];
AudioClip g_toneClips[kToneCount];
AudioClip g_packClips[AudioMixer::kMaxVoices];
}  // namespace

bool RuntimeAudio::init(IFileSystem* fs, const PackConsoleTarget* ct, AudioMixer::ClockUsFn clock) {
  u32 rate = ct && ct->audioSampleRate ? ct->audioSampleRate : 22050;
  const u32 wantVoices = ct && ct->audioVoices ? ct->audioVoices : 16;
  if (wantVoices > AudioMixer::kMaxVoices)
    RLOG_WARN("audio", "target wants %u voices, this build mixes %u (RENSGA_AUDIO_MAX_VOICES)", wantVoices, AudioMixer::kMaxVoices);
  if (!mixer_.init(rate, fs)) return false;
  mixer_.setClock(clock);
  AudioSystem::setMixer(&mixer_);
  backend_ = &g_backend;
  deviceOk_ = backend_->init(rate, &mixer_);
  if (!deviceOk_) RLOG_WARN("audio", "no audio device, running silent");
  RLOG_INFO("audio", "rensga: audio rate=%u voices=%u device=%s", rate, AudioMixer::kMaxVoices, deviceOk_ ? "ok" : "none");
  return deviceOk_;
}

void RuntimeAudio::shutdown() {
  AudioSystem::setMixer(nullptr);
  if (backend_) backend_->shutdown();
  mixer_.shutdown();
  backend_ = nullptr;
}

void RuntimeAudio::update() {
  if (backend_ && deviceOk_) backend_->update();
}

void RuntimeAudio::registerPackClips(const PackAssets& as) {
  AudioSystem::clearClips();
  u32 n = 0;
  for (u32 i = 0; i < as.audioCount; ++i) {
    const PackAudioRec& r = as.audio[i];
    AudioClip c;
    c.samples = as.audioSamples(r);
    c.frames = r.frameCount;
    c.sampleRate = r.sampleRate;
    c.channels = static_cast<u8>(r.channels);
    if (r.loops) { c.loopStart = static_cast<i32>(r.loopStart); c.loopEnd = static_cast<i32>(r.loopEnd); }
    if (AudioSystem::registerClip(r.guid, c)) ++n;
  }
  if (n) RLOG_INFO("audio", "rensga: registered %u audio clips", n);
}

bool RuntimeAudio::startMusic(Scalar volume) {
  musicOn_ = mixer_.playMusic(kMusicPath, volume, true);
  RLOG_INFO("audio", "rensga: music %s %s", kMusicPath, musicOn_ ? "streaming" : "not found");
  return musicOn_;
}

bool RuntimeAudio::beginStress(const PackAssets* as) {
  const AudioClip* clips = g_toneClips;
  u32 count = kToneCount;
  if (as && as->audioCount) {
    count = as->audioCount < AudioMixer::kMaxVoices ? as->audioCount : AudioMixer::kMaxVoices;
    for (u32 i = 0; i < count; ++i) {
      const PackAudioRec& r = as->audio[i];
      AudioClip& c = g_packClips[i];
      c = AudioClip();
      c.samples = as->audioSamples(r);
      c.frames = r.frameCount;
      c.sampleRate = r.sampleRate;
      c.channels = static_cast<u8>(r.channels);
    }
    clips = g_packClips;
  } else {
    for (u32 t = 0; t < kToneCount; ++t) {
      const u32 period = 40 + t * 17;  // different pitches
      for (u32 i = 0; i < kToneFrames; ++i) g_tones[t][i] = static_cast<i16>(((i % period) < period / 2) ? 2500 : -2500);
      g_toneClips[t].samples = g_tones[t];
      g_toneClips[t].frames = kToneFrames;
    }
  }
  startMusic(0.5f);
  u32 started = 0;
  for (u32 v = 0; v < AudioMixer::kMaxVoices; ++v) {
    const Scalar pan = (static_cast<Scalar>(v) - 7.5f) / 7.5f;
    started += mixer_.play(&clips[v % count], 0.12f, pan, true, 1.0f) ? 1 : 0;
  }
  RLOG_INFO("audio", "rensga: audio-stress start effects=%u music=%d clips=%u", started, musicOn_ ? 1 : 0, count);
  mixer_.resetMixStats();
  return started == AudioMixer::kMaxVoices;
}

void RuntimeAudio::logStress(u32 frames) {
  const AudioMixer::MixStats& s = mixer_.mixStats();
  const u32 avg = s.blocks ? static_cast<u32>(s.totalUs / s.blocks) : 0;
  const u32 fpb = s.blocks ? static_cast<u32>(s.frames / s.blocks) : 0;
  // CPU share of one core: mix time per block divided by the playback time of that block.
  const u32 permille = fpb ? static_cast<u32>((static_cast<u64>(avg) * mixer_.sampleRate()) / (static_cast<u64>(fpb) * 1000)) : 0;
  RLOG_INFO("audio", "rensga: audio-stress voices=%u mix_us=%u/%u blocks=%u frames_per_block=%u cpu_permille=%u music=%d underruns=%u frames=%u",
            mixer_.activeVoices(), avg, static_cast<u32>(s.maxUs), static_cast<u32>(s.blocks), fpb, permille, mixer_.musicPlaying() ? 1 : 0,
            mixer_.musicUnderruns(), frames);
}

}  // namespace rensga
