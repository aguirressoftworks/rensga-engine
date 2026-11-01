// F6.10 (B1): AudioMixer groups reach the mixer (Master / Music / SFX volumes, mute, dB), AudioSource playOnAwake and mute,
// PlayClipAtPoint, and the sample-exact behaviour of the integer mixer.
#include <stdlib.h>

#include "rensga/Audio.h"
#include "rensga/core/animator_system.h"
#include "rensga/core/components.h"
#include "rensga/core/loop.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
i16 g_samples[512];
AudioClip makeClip() {
  for (i16& s : g_samples) s = 8000;
  AudioClip c;
  c.samples = g_samples;
  c.frames = 512;
  c.sampleRate = 22050;
  c.channels = 1;
  return c;
}
Guid gid(u8 n) {
  Guid g;
  g.bytes[0] = 0xA5;
  g.bytes[15] = n;
  return g;
}
// Mixes a block and returns the left sample of the last frame (a constant clip gives a constant output).
i32 mixLeft(AudioMixer& m) {
  i16 out[64 * 2];
  m.mix(out, 64);
  return out[63 * 2];
}
struct MixFixture {
  AudioMixer mixer;
  AudioClip clip = makeClip();
  MixFixture() {
    mixer.init(22050, nullptr);
    AudioSystem::setMixer(&mixer);
    AudioSystem::clearClips();
    AudioSystem::registerClip(gid(1), clip);
    AudioMixerGroups::ResetAll();
  }
  ~MixFixture() {
    AudioMixerGroups::ResetAll();
    AudioSystem::setMixer(nullptr);
    AudioSystem::clearClips();
  }
};
}  // namespace

TEST(mixer_groups_scale_voices_music_and_master) {
  MixFixture f;
  const AudioClip* c = AudioSystem::findClip(gid(1));
  f.mixer.play(c, 1.0f, 0.0f, true, 1.0f, 128, AudioGroup::SFX);
  const i32 full = mixLeft(f.mixer);
  CHECK(full > 4000);
  AudioMixerGroups::SetVolume(AudioGroup::SFX, 0.5f);
  CHECK_NEAR(static_cast<Scalar>(mixLeft(f.mixer)), static_cast<Scalar>(full) * 0.5f, 3.0f);
  AudioMixerGroups::SetVolume(AudioGroup::Music, 0.0f);  // other group: no effect on an SFX voice
  CHECK_NEAR(static_cast<Scalar>(mixLeft(f.mixer)), static_cast<Scalar>(full) * 0.5f, 3.0f);
  AudioMixerGroups::SetVolume(AudioGroup::Master, 0.5f);
  CHECK_NEAR(static_cast<Scalar>(mixLeft(f.mixer)), static_cast<Scalar>(full) * 0.25f, 3.0f);
  AudioMixerGroups::SetMuted(AudioGroup::SFX, true);
  CHECK_EQ(mixLeft(f.mixer), 0);
  CHECK(AudioMixerGroups::IsMuted(AudioGroup::SFX));
  CHECK_NEAR(AudioMixerGroups::GetVolume(AudioGroup::SFX), 0.5f, 1e-6f);  // muting keeps the volume
  AudioMixerGroups::SetMuted(AudioGroup::SFX, false);
  CHECK(mixLeft(f.mixer) > 0);
  AudioMixerGroups::ResetAll();
  CHECK_NEAR(static_cast<Scalar>(mixLeft(f.mixer)), static_cast<Scalar>(full), 3.0f);
}

TEST(mixer_group_music_voice_follows_the_music_group_only) {
  MixFixture f;
  const AudioClip* c = AudioSystem::findClip(gid(1));
  f.mixer.play(c, 1.0f, 0.0f, true, 1.0f, 128, AudioGroup::Music);
  const i32 full = mixLeft(f.mixer);
  AudioMixerGroups::SetVolume(AudioGroup::SFX, 0.0f);
  CHECK_EQ(mixLeft(f.mixer), full);
  AudioMixerGroups::SetVolume(AudioGroup::Music, 0.25f);
  CHECK_NEAR(static_cast<Scalar>(mixLeft(f.mixer)), static_cast<Scalar>(full) * 0.25f, 3.0f);
}

TEST(mixer_group_volumes_survive_binding_a_new_mixer_and_convert_from_db) {
  AudioMixerGroups::ResetAll();
  AudioMixerGroups::SetVolume(AudioGroup::Music, 0.3f);  // no mixer bound yet
  AudioMixer m;
  m.init(22050, nullptr);
  AudioSystem::setMixer(&m);
  CHECK_NEAR(m.groupVolume(AudioGroup::Music), 0.3f, 1e-6f);
  AudioSystem::setMixer(nullptr);
  AudioMixerGroups::ResetAll();
  CHECK_NEAR(AudioMixerGroups::DbToLinear(0.0f), 1.0f, 1e-6f);
  CHECK_NEAR(AudioMixerGroups::DbToLinear(-6.0f), 0.5012f, 1e-3f);
  CHECK_NEAR(AudioMixerGroups::DbToLinear(-20.0f), 0.1f, 1e-4f);
  CHECK_NEAR(AudioMixerGroups::DbToLinear(-10.5f), 0.2985f, 2e-3f);  // interpolated between table entries
  CHECK_NEAR(AudioMixerGroups::DbToLinear(6.0f), 1.9953f, 1e-3f);
  CHECK_EQ(AudioMixerGroups::DbToLinear(-80.0f), 0.0f);
  CHECK_EQ(AudioMixerGroups::DbToLinear(-200.0f), 0.0f);
  AudioMixerGroups::SetVolumeDb(AudioGroup::SFX, -6.0f);
  CHECK_NEAR(AudioMixerGroups::GetVolume(AudioGroup::SFX), 0.5012f, 1e-3f);
  AudioMixerGroups::ResetAll();
}

namespace {
struct SrcWorld {
  Arena arena;
  World world;
  GameLoop loop;
  VirtualGamepad pad;
  SrcWorld() {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 16;
    c.maxComponentsPerType = 8;
    world.init(c, arena);
    loop.init(world);
  }
  ~SrcWorld() { world.shutdown(); }
  void frames(int n) { for (int i = 0; i < n; ++i) loop.frame(1.0f / 60.0f, pad); }
};
}  // namespace

TEST(audio_source_play_on_awake_mute_and_output_group) {
  MixFixture f;
  SrcWorld w;
  GameObject* a = w.world.createObject("A");
  AudioSource* sa = a->AddComponent<AudioSource>();
  sa->clip = gid(1);
  sa->loop = true;
  sa->playOnAwake = true;
  sa->outputGroup = 2;
  GameObject* b = w.world.createObject("B");
  AudioSource* sb = b->AddComponent<AudioSource>();
  sb->clip = gid(1);
  sb->playOnAwake = false;  // the default: scenes from before F6 stay silent
  w.frames(2);
  CHECK(sa->isPlaying());
  CHECK(!sb->isPlaying());
  CHECK_EQ(f.mixer.activeVoices(), 1u);
  const i32 full = mixLeft(f.mixer);
  sa->mute = true;
  w.frames(1);
  CHECK_EQ(mixLeft(f.mixer), 0);
  sa->mute = false;
  w.frames(1);
  CHECK_NEAR(static_cast<Scalar>(mixLeft(f.mixer)), static_cast<Scalar>(full), 3.0f);
  AudioMixerGroups::SetVolume(AudioGroup::SFX, 0.0f);  // outputGroup 2 = SFX
  CHECK_EQ(mixLeft(f.mixer), 0);
  AudioMixerGroups::ResetAll();
  sa->Stop();
  w.frames(2);
  CHECK(!sa->isPlaying());  // played once at awake only
  GameObject* c = w.world.createObject("C");  // inactive objects do not play
  c->SetActive(false);
  AudioSource* sc = c->AddComponent<AudioSource>();
  sc->clip = gid(1);
  sc->playOnAwake = true;
  w.frames(2);
  CHECK(!sc->isPlaying());
  c->SetActive(true);
  w.frames(2);
  CHECK(sc->isPlaying());
}

TEST(play_clip_at_point_pans_and_attenuates_from_the_camera) {
  MixFixture f;
  SrcWorld w;
  GameObject* cam = w.world.createObject("Cam");
  cam->AddComponent<Camera>();
  CHECK(!AudioSource::PlayClipAtPoint(gid(9), Vector3(0, 0, 5), 1.0f));  // unknown clip
  CHECK(AudioSource::PlayClipAtPoint(gid(1), Vector3(0, 0, 0.5f), 1.0f));
  const i32 near0 = mixLeft(f.mixer);
  f.mixer.stopAll();
  CHECK(AudioSource::PlayClipAtPoint(gid(1), Vector3(21, 0, 0), 1.0f));  // to the right of a camera looking down +z
  i16 out[64 * 2];
  f.mixer.mix(out, 64);
  CHECK(out[63 * 2 + 1] > out[63 * 2]);          // pans right
  CHECK(out[63 * 2 + 1] < near0);                // about half way to the far plane: quieter
  f.mixer.stopAll();
  CHECK(!AudioSource::PlayClipAtPoint(gid(1), Vector3(0, 0, 80), 1.0f));  // beyond 40 units: silent, no voice
  CHECK_EQ(f.mixer.activeVoices(), 0u);
}
