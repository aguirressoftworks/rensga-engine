// F5.17: AudioMixer voice stealing by priority and the per-target voice limit.
#include "rensga/core/audio.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
i16 g_samples[4096];
AudioClip clip() {
  for (u32 i = 0; i < 4096; ++i) g_samples[i] = static_cast<i16>((i & 1) ? 1000 : -1000);
  AudioClip c;
  c.samples = g_samples;
  c.frames = 4096;
  return c;
}
AudioMixer& fresh(u32 limit) {
  static AudioMixer m;
  m.shutdown();
  m.init(22050, nullptr);
  m.setVoiceLimit(limit);
  return m;
}
}  // namespace

TEST(audio_priority_steals_lowest_priority_then_oldest) {
  AudioClip c = clip();
  AudioMixer& m = fresh(4);
  const u32 a = m.play(&c, 1, 0, false, 1, 10);   // important
  const u32 b = m.play(&c, 1, 0, false, 1, 200);  // oldest of the low ones
  const u32 d = m.play(&c, 1, 0, false, 1, 200);
  const u32 e = m.play(&c, 1, 0, false, 1, 100);
  CHECK(a && b && d && e);
  CHECK_EQ(m.activeVoices(), 4u);
  const u32 f = m.play(&c, 1, 0, false, 1, 50);  // steals priority 200, the older one (b)
  CHECK(f != 0);
  CHECK(!m.isPlaying(b));
  CHECK(m.isPlaying(a) && m.isPlaying(d) && m.isPlaying(e) && m.isPlaying(f));
  CHECK_EQ(m.stolenVoices(), 1u);
  const u32 g = m.play(&c, 1, 0, false, 1, 50);  // next victim is d (still 200)
  CHECK(g != 0);
  CHECK(!m.isPlaying(d));
  CHECK_EQ(m.stolenVoices(), 2u);
}

TEST(audio_priority_never_steals_a_more_important_voice) {
  AudioClip c = clip();
  AudioMixer& m = fresh(2);
  const u32 a = m.play(&c, 1, 0, false, 1, 5);
  const u32 b = m.play(&c, 1, 0, false, 1, 20);
  CHECK(a && b);
  CHECK_EQ(m.play(&c, 1, 0, false, 1, 100), 0u);  // both are more important than 100
  CHECK(m.isPlaying(a) && m.isPlaying(b));
  CHECK_EQ(m.stolenVoices(), 0u);
  CHECK(m.play(&c, 1, 0, false, 1, 20) != 0);     // equal priority, non-looping: the oldest equal goes (b)
  CHECK(!m.isPlaying(b) && m.isPlaying(a));
}

TEST(audio_priority_default_keeps_looping_voices) {
  AudioClip c = clip();
  AudioMixer& m = fresh(2);
  const u32 a = m.play(&c, 1, 0, true);
  const u32 b = m.play(&c, 1, 0, true);
  CHECK(a && b);
  CHECK_EQ(m.play(&c), 0u);                       // every voice loops at the same priority: nothing to steal
  CHECK(m.play(&c, 1, 0, false, 1, 0) != 0u);     // priority 0 outranks the looping 128s and takes the oldest
  CHECK_EQ(m.stolenVoices(), 1u);
  CHECK(!m.isPlaying(a) && m.isPlaying(b));
}

TEST(audio_voice_limit_follows_the_target) {
  AudioClip c = clip();
  AudioMixer& m = fresh(8);
  for (int i = 0; i < 8; ++i) CHECK(m.play(&c, 1, 0, true, 1, 100) != 0);
  CHECK_EQ(m.activeVoices(), 8u);
  CHECK_EQ(m.play(&c, 1, 0, true, 1, 100), 0u);   // limit reached, all loop at equal priority
  m.setVoiceLimit(4);
  CHECK_EQ(m.activeVoices(), 4u);
  CHECK_EQ(m.voiceLimit(), 4u);
  m.setVoiceLimit(1000);
  CHECK_EQ(m.voiceLimit(), AudioMixer::kMaxVoices);
}
