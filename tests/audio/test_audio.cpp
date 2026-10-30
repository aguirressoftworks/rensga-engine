#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rensga/Audio.h"
#include "rensga/core/audio.h"
#include "rensga/core/memory.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
AudioClip makeClip(const i16* s, u32 frames, u8 ch = 1, u32 rate = 22050) {
  AudioClip c;
  c.samples = s;
  c.frames = frames;
  c.channels = ch;
  c.sampleRate = rate;
  return c;
}

AudioMixer& freshMixer(IFileSystem* fs = nullptr, u32 rate = 22050) {
  static AudioMixer m;
  m.shutdown();
  m.init(rate, fs);
  return m;
}

u32 fnv(u32 h, const i16* p, u32 n) {
  for (u32 i = 0; i < n; ++i) {
    h ^= static_cast<u16>(p[i]) & 0xFF;
    h *= 16777619u;
    h ^= (static_cast<u16>(p[i]) >> 8);
    h *= 16777619u;
  }
  return h;
}

// Temp folder with a generated WAV.
struct TempDir {
  char path[64];
  TempDir() {
    snprintf(path, sizeof(path), "/tmp/rensga_audio_test_%d/", static_cast<int>(getpid()));
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", path);
    if (system(cmd) != 0) path[0] = 0;
  }
  ~TempDir() {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
    if (path[0] && system(cmd) != 0) {}
  }
};

void put32(FILE* f, u32 v) { fwrite(&v, 4, 1, f); }
void put16(FILE* f, u16 v) { fwrite(&v, 2, 1, f); }

// Ramp WAV: sample value = (frame * 7) % 20000 in all channels.
void writeWav(const char* file, u32 frames, u32 channels, u32 rate) {
  FILE* f = fopen(file, "wb");
  if (!f) return;
  const u32 bytes = frames * channels * 2;
  fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f);
  put32(f, 16); put16(f, 1); put16(f, channels); put32(f, rate); put32(f, rate * channels * 2);
  put16(f, channels * 2); put16(f, 16); fwrite("data", 1, 4, f); put32(f, bytes);
  for (u32 i = 0; i < frames; ++i)
    for (u32 c = 0; c < channels; ++c) put16(f, static_cast<u16>((i * 7) % 20000));
  fclose(f);
}
}  // namespace

TEST(audio_single_voice_center_pan_is_constant_power) {
  static i16 s[64];
  for (auto& v : s) v = 10000;
  AudioClip c = makeClip(s, 64);
  AudioMixer& m = freshMixer();
  CHECK(m.play(&c) != 0);
  i16 out[16];
  m.mix(out, 8);
  CHECK_NEAR(int(out[0]), 7071, 3);
  CHECK_NEAR(int(out[1]), 7071, 3);
}

TEST(audio_pan_hard_left_and_right) {
  static i16 s[64];
  for (auto& v : s) v = 10000;
  AudioClip c = makeClip(s, 64);
  AudioMixer& m = freshMixer();
  m.play(&c, 1.0f, -1.0f);
  i16 out[8];
  m.mix(out, 4);
  CHECK_EQ(int(out[0]), 10000);
  CHECK(out[1] < 5);
  m.stopAll();
  m.play(&c, 0.5f, 1.0f);
  m.mix(out, 4);
  CHECK(out[0] < 5);
  CHECK_NEAR(int(out[1]), 5000, 2);
}

TEST(audio_one_shot_ends_and_id_goes_stale) {
  static i16 s[100];
  for (auto& v : s) v = 1000;
  AudioClip c = makeClip(s, 100);
  AudioMixer& m = freshMixer();
  u32 id = m.play(&c);
  CHECK(m.isPlaying(id));
  i16 out[600];
  m.mix(out, 300);
  CHECK(!m.isPlaying(id));
  CHECK_EQ(int(m.activeVoices()), 0);
  CHECK(out[2 * 150] == 0);  // silent after the end
}

TEST(audio_loop_points_keep_playback_inside_the_loop) {
  static i16 s[200];
  for (int i = 0; i < 200; ++i) s[i] = static_cast<i16>(i);  // sample value = frame index
  AudioClip c = makeClip(s, 200);
  c.loopStart = 50;
  c.loopEnd = 80;
  AudioMixer& m = freshMixer();
  u32 id = m.play(&c, 1.0f, -1.0f, true);  // pan -1: left gain is exactly 1.0
  static i16 out[2 * 1000];
  m.mix(out, 1000);
  CHECK(m.isPlaying(id));
  for (int i = 90; i < 1000; ++i) CHECK(out[i * 2] >= 50 && out[i * 2] < 80);
  CHECK_EQ(int(out[2 * 0]), 0);   // the intro before the loop start plays once
  CHECK_EQ(int(out[2 * 79]), 79);
}

TEST(audio_pitch_doubles_the_speed) {
  static i16 s[400];
  for (int i = 0; i < 400; ++i) s[i] = static_cast<i16>(i * 10);
  AudioClip c = makeClip(s, 400);
  AudioMixer& m = freshMixer();
  m.play(&c, 1.0f, -1.0f, false, 2.0f);
  i16 out[2 * 10];
  m.mix(out, 10);
  CHECK_EQ(int(out[2 * 3]), 60);
}

TEST(audio_mixing_saturates_instead_of_wrapping) {
  static i16 hi[32], lo[32];
  for (auto& v : hi) v = 32767;
  for (auto& v : lo) v = -32768;
  AudioClip ch = makeClip(hi, 32), cl = makeClip(lo, 32);
  AudioMixer& m = freshMixer();
  for (int i = 0; i < 16; ++i) m.play(&ch, 1.0f, -1.0f, true);
  i16 out[8];
  m.mix(out, 4);
  CHECK_EQ(int(out[0]), 32767);
  m.stopAll();
  for (int i = 0; i < 16; ++i) m.play(&cl, 1.0f, -1.0f, true);
  m.mix(out, 4);
  CHECK_EQ(int(out[0]), -32768);
}

TEST(audio_voice_stealing_takes_the_oldest_non_looping) {
  static i16 s[22050];
  for (auto& v : s) v = 100;
  AudioClip c = makeClip(s, 22050);
  AudioMixer& m = freshMixer();
  u32 ids[AudioMixer::kMaxVoices];
  for (u32 i = 0; i < AudioMixer::kMaxVoices; ++i) ids[i] = m.play(&c);
  CHECK_EQ(int(m.activeVoices()), int(AudioMixer::kMaxVoices));
  u32 extra = m.play(&c);
  CHECK(extra != 0);
  CHECK_EQ(int(m.stolenVoices()), 1);
  CHECK(!m.isPlaying(ids[0]));
  CHECK(m.isPlaying(ids[1]));
  CHECK(m.isPlaying(extra));
  // All looping: nothing may be stolen.
  m.stopAll();
  for (u32 i = 0; i < AudioMixer::kMaxVoices; ++i) m.play(&c, 1.0f, 0.0f, true);
  CHECK_EQ(int(m.play(&c, 1.0f, 0.0f, true)), 0);
}

TEST(audio_music_streams_loops_and_ends) {
  TempDir t;
  char wav[128];
  snprintf(wav, sizeof(wav), "%smusic.wav", t.path);
  writeWav(wav, 22050, 2, 22050);  // 1 s stereo
  StdioFileSystem fs(t.path);
  AudioMixer& m = freshMixer(&fs);
  CHECK(m.playMusic("data:/music.wav", 1.0f, false));
  static i16 out[2 * 512];
  u32 frames = 0;
  while (m.musicPlaying() && frames < 22050 * 3) {
    m.update();
    m.mix(out, 512);
    frames += 512;
  }
  CHECK(!m.musicPlaying());                    // ended
  CHECK(frames >= 22050 && frames < 22050 + 2048);
  CHECK_EQ(int(m.musicUnderruns()), 0);
  // Looping keeps going.
  CHECK(m.playMusic("data:/music.wav", 1.0f, true));
  for (int i = 0; i < 200; ++i) { m.update(); m.mix(out, 512); }  // 4.6 s
  CHECK(m.musicPlaying());
  CHECK_EQ(int(m.musicUnderruns()), 0);
  m.stopMusic();
  CHECK(!m.playMusic("data:/missing.wav"));
}

TEST(audio_music_resamples_and_expands_mono) {
  TempDir t;
  char wav[128];
  snprintf(wav, sizeof(wav), "%smono.wav", t.path);
  writeWav(wav, 11025, 1, 11025);  // 1 s mono at half the mixer rate
  StdioFileSystem fs(t.path);
  AudioMixer& m = freshMixer(&fs);
  CHECK(m.playMusic("mono.wav", 1.0f, false));
  static i16 out[2 * 512];
  u32 frames = 0;
  bool leftEqualsRight = true;
  while (m.musicPlaying() && frames < 22050 * 2) {
    m.update();
    m.mix(out, 512);
    for (int i = 0; i < 512; ++i) leftEqualsRight &= out[2 * i] == out[2 * i + 1];
    frames += 512;
  }
  CHECK(leftEqualsRight);
  CHECK(frames >= 22050 && frames < 22050 + 2048);  // 1 s of source = 1 s of output
}

namespace {
struct StressResult { u32 hash; u32 underruns; u32 heapDelta; u32 peak; };

StressResult runStress() {
  TempDir t;
  char wav[128];
  snprintf(wav, sizeof(wav), "%sbgm.wav", t.path);
  writeWav(wav, 22050 * 3, 2, 22050);
  StdioFileSystem fs(t.path);

  static i16 clips[4][4000];
  for (int k = 0; k < 4; ++k)
    for (int i = 0; i < 4000; ++i) clips[k][i] = static_cast<i16>(((i * (k + 3)) % 200 - 100) * (80 + 40 * k));
  AudioClip c[4];
  for (int k = 0; k < 4; ++k) c[k] = makeClip(clips[k], 4000, 1, k == 3 ? 11025 : 22050);
  c[2].loopStart = 500;
  c[2].loopEnd = 3000;

  AudioMixer& m = freshMixer(&fs);
  const u64 heap0 = heap::allocationCount();
  m.playMusic("data:/bgm.wav", 0.6f, true);
  u32 h = 2166136261u;
  u32 peak = 0;
  static i16 out[2 * 441];
  const u32 blocks = 22050 * 10 / 441;  // 10 s
  for (u32 b = 0; b < blocks; ++b) {
    for (int k = 0; k < 3; ++k) {  // 3 new voices per block: far more than 16 live at once
      const u32 n = b * 3 + k;
      m.play(&c[n & 3], 0.5f + 0.1f * (n % 5), -1.0f + 0.25f * (n % 9), (n % 7) == 0, 0.5f + 0.25f * (n % 6));
    }
    m.update();
    m.mix(out, 441);
    h = fnv(h, out, 441 * 2);
    for (int i = 0; i < 441 * 2; ++i) {
      const u32 a = out[i] < 0 ? u32(-int(out[i])) : u32(out[i]);
      if (a > peak) peak = a;
    }
    if (b == blocks / 2) CHECK_EQ(int(m.activeVoices()), int(AudioMixer::kMaxVoices));
  }
  StressResult r;
  r.hash = h;
  r.underruns = m.musicUnderruns();
  r.heapDelta = static_cast<u32>(heap::allocationCount() - heap0);
  r.peak = peak;
  CHECK(m.stolenVoices() > 0);
  CHECK(m.musicPlaying());
  return r;
}
}  // namespace

TEST(audio_16_voice_stress_10s_is_deterministic_without_heap) {
  const StressResult a = runStress();
  const StressResult b = runStress();
  CHECK_EQ(int(a.heapDelta), 0);
  CHECK_EQ(int(a.underruns), 0);
  CHECK(a.peak > 1000);          // not silent
  CHECK(a.peak <= 32768);        // no overflow wrap (saturating)
  CHECK_EQ(a.hash, b.hash);      // same calls, same bytes
  printf("  stress hash=%08x peak=%u\n", a.hash, a.peak);
}

TEST(audio_source_component_plays_through_the_system_mixer) {
  static i16 s[200];
  for (auto& v : s) v = 5000;
  AudioMixer& m = freshMixer();
  AudioSystem::clearClips();
  AudioSystem::setMixer(&m);
  Guid g;
  g.bytes[0] = 0x42;
  CHECK(AudioSystem::registerClip(g, makeClip(s, 200)));
  AudioSource src;
  src.clip = g;
  src.Play();
  CHECK(src.isPlaying());
  src.Stop();
  CHECK(!src.isPlaying());
  src.PlayOneShot(g);
  CHECK_EQ(int(m.activeVoices()), 1);
  AudioSystem::setMixer(nullptr);
  AudioSystem::clearClips();
}

namespace {
u64 g_fakeClock = 0;
u64 fakeClock() { return g_fakeClock += 7; }
}  // namespace

TEST(audio_mix_stats_use_the_injected_clock_and_do_not_change_the_output) {
  static i16 s[300];
  for (u32 i = 0; i < 300; ++i) s[i] = static_cast<i16>((i % 20) * 300);
  const AudioClip c = makeClip(s, 300);
  i16 a[512], b[512];
  AudioMixer& m = freshMixer();
  m.play(&c, 1.0f, 0.0f, true);
  m.mix(a, 256);
  AudioMixer& m2 = freshMixer();
  m2.setClock(fakeClock);
  m2.play(&c, 1.0f, 0.0f, true);
  m2.mix(b, 256);
  CHECK_EQ(memcmp(a, b, sizeof(a)), 0);
  CHECK_EQ(m2.mixStats().blocks, 1u);
  CHECK_EQ(m2.mixStats().frames, 256u);
  CHECK_EQ(m2.mixStats().maxUs, 7u);
  m2.setClock(nullptr);
}
