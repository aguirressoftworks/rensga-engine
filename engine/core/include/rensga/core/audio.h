// Engine 4: software audio mixer. Pure and deterministic: integer mixing only, no libm, no heap
// after init (every buffer is a member), same output on every platform for the same calls.
//
// Threading model: AudioMixer is NOT thread safe and is only touched from the main thread. A
// backend pulls samples by calling mix() from the main thread too (SDL: into a lock-free ring that
// the device callback drains; PS2: straight into audsrv). The music stream refills in update(),
// also on the main thread, so the audio thread (SDL) never touches the file system. This matters
// on PS2 where IOP file access from a callback is not allowed.
//
// SPU2 vs software mixing: the PS2 SPU2 and the PSP/PSX SPU can mix ADPCM voices in hardware for
// free. We still mix in software (PCM16) for now: one code path, bit-identical output on every
// profile (RULES 1.9), and 16 voices at 22 kHz cost little CPU. Hardware voices (ADPCM in SPU RAM,
// the PSX SPU budget) are a later optimisation behind this same interface (PSX Engine, PS2 Engine).
//
// Pan law: constant power. angle = (pan + 1) * pi / 4, gainL = cos(angle), gainR = sin(angle), so
// centre is 0.7071 per side and the perceived loudness stays the same across the pan range.
// Voice stealing (F5.17): when all voices are busy, play() steals from the voices of lower or equal importance. Priority is
// Unity's: 0 = most important .. 255 = least. The victim is the voice with the highest priority number, then the oldest.
// A voice of higher importance (a smaller number) is never stolen. A looping voice of EQUAL priority is kept, so with every
// priority at the default 128 this is the old rule: the oldest non-looping voice is replaced, and play() returns 0 when every
// voice loops. The voice budget comes from the ConsoleTarget (CTGT audioVoices): setVoiceLimit().
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/types.h"

#ifndef RENSGA_AUDIO_MAX_VOICES
#define RENSGA_AUDIO_MAX_VOICES 16
#endif

namespace rensga {

// Decoded sound effect (PCM16, interleaved if stereo). Samples live in the pack or a caller buffer.
struct AudioClip {
  const i16* samples = nullptr;
  u32 frames = 0;       // samples per channel
  u32 sampleRate = 22050;
  u8 channels = 1;      // 1 or 2
  i32 loopStart = -1;   // frame index; -1 = loop the whole clip
  i32 loopEnd = -1;     // frame index (exclusive); -1 = clip end
};

class AudioMixer {
 public:
  static constexpr u32 kMaxVoices = RENSGA_AUDIO_MAX_VOICES;
  static constexpr u32 kMusicChunkFrames = 4096;  // source frames per streamed chunk (double buffered)

  AudioMixer();
  // sampleRate: 22050 or 44100 (from the ConsoleTarget). `fs` is used for music streams only (may be null).
  bool init(u32 sampleRate, IFileSystem* fs);
  void shutdown();  // closes the music stream

  // ---- sound effects. Returns a voice id (non-zero), or 0 when nothing could be started.
  // volume 0..2, pan -1 (left) .. 1 (right), pitch 0.25..4 (1 = unchanged).
  u32 play(const AudioClip* clip, Scalar volume = 1.0f, Scalar pan = 0.0f, bool loop = false, Scalar pitch = 1.0f, u8 priority = 128);
  void stop(u32 voice);
  void stopAll();
  bool isPlaying(u32 voice) const;  // false for stale ids (the voice ended or was stolen)
  void setVolume(u32 voice, Scalar volume);
  void setPan(u32 voice, Scalar pan);
  void setPitch(u32 voice, Scalar pitch);
  u32 activeVoices() const;
  // F5.17: voices this target may use (CTGT audioVoices); clamped to 1..kMaxVoices. Voices above the new limit are stopped.
  void setVoiceLimit(u32 voices);
  u32 voiceLimit() const { return limit_; }
  u32 stolenVoices() const { return stolen_; }

  // ---- one streamed music voice (extra to the effect voices). `path` is a virtual path ("data:/music/a.wav").
  // WAV (PCM16 mono/stereo) or raw stereo PCM16 at the mixer rate. Loops by seeking back to the data start.
  bool playMusic(const char* path, Scalar volume = 1.0f, bool loop = true);
  void stopMusic();
  bool musicPlaying() const { return music_.active; }
  void setMusicVolume(Scalar volume);
  u32 musicUnderruns() const { return music_.underruns; }

  void setMasterVolume(Scalar volume);  // 0..2

  // Main thread, once per frame: refills the music chunks that were consumed.
  void update();
  // Writes `frames` stereo frames (L,R interleaved int16). Saturates, never wraps.
  void mix(i16* out, u32 frames);

  u32 sampleRate() const { return rate_; }

  // ---- optional CPU measurement (Engine 4 exercise). A runtime main passes a microsecond clock; with none set mix()
  // measures nothing and stays pure. The numbers never feed back into the mixed samples.
  typedef u64 (*ClockUsFn)();
  struct MixStats {
    u64 blocks = 0, frames = 0, totalUs = 0, maxUs = 0;
  };
  void setClock(ClockUsFn fn) { clock_ = fn; }
  const MixStats& mixStats() const { return stats_; }
  void resetMixStats() { stats_ = MixStats(); }

 private:
  struct Voice {
    const AudioClip* clip = nullptr;
    u32 index = 0;      // integer frame position
    u32 frac = 0;       // 16.16 fraction
    u32 step = 0;       // 16.16 source frames per output frame
    i32 gainL = 0, gainR = 0;  // Q15
    u32 serial = 0;     // start order (voice stealing)
    u16 generation = 0;
    u8 priority = 128;  // 0 = most important
    bool active = false, loop = false;
    Scalar volume = 1, pan = 0, pitch = 1;
  };
  struct Music {
    IFileSystem* fs = nullptr;
    i32 handle = -1;
    bool active = false, loop = true, eof = false;
    u32 dataOffset = 0, dataBytes = 0, dataLeft = 0;
    u32 srcRate = 0, channels = 2;
    u32 step = 0, frac = 0;     // 16.16
    u32 cur = 0, pos = 0;       // playing buffer, frame position inside it
    u32 frames[2] = {0, 0};
    bool filled[2] = {false, false};
    i32 gainL = 0, gainR = 0;
    Scalar volume = 1;
    u32 underruns = 0;
    alignas(16) i16 buf[2][kMusicChunkFrames * 2];  // stereo
  };

  static constexpr u32 kMixChunk = 256;
  void computeGains(Voice& v);
  u32 voiceId(u32 index) const;
  Voice* lookup(u32 id);
  const Voice* lookup(u32 id) const;
  bool refillMusic(u32 which);
  void mixVoice(Voice& v, i32* acc, u32 frames);
  void mixMusic(i32* acc, u32 frames);

  ClockUsFn clock_ = nullptr;
  MixStats stats_;
  u32 rate_ = 0;
  u32 serial_ = 0;
  u32 stolen_ = 0;
  u32 limit_ = kMaxVoices;
  i32 master_ = 32768;  // Q15
  IFileSystem* fs_ = nullptr;
  Voice voices_[kMaxVoices];
  Music music_;
};

}  // namespace rensga
