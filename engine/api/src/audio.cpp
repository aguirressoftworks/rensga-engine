#include "rensga/Audio.h"
#include "rensga/core/animator_system.h"
#include "rensga/core/components.h"
#include "rensga/core/world.h"

namespace rensga {
namespace {
AudioMixer* g_mixer = nullptr;
struct Entry { Guid guid; AudioClip clip; bool used = false; };
Entry g_clips[AudioSystem::kMaxClips];
AudioGroup groupOf(i32 g) { return g == 1 ? AudioGroup::Music : (g == 2 ? AudioGroup::SFX : AudioGroup::Master); }
u8 prio(i32 p) { return static_cast<u8>(p < 0 ? 0 : (p > 255 ? 255 : p)); }  // AudioSource.priority, F5.17
}  // namespace

void AudioSystem::setMixer(AudioMixer* mixer) {
  g_mixer = mixer;
  AudioMixerGroups::Apply();  // F6.10: the stored group volumes reach a newly bound mixer
}
AudioMixer* AudioSystem::mixer() { return g_mixer; }

bool AudioSystem::registerClip(const Guid& guid, const AudioClip& clip) {
  Entry* freeSlot = nullptr;
  for (Entry& e : g_clips) {
    if (e.used && e.guid == guid) { e.clip = clip; return true; }
    if (!e.used && !freeSlot) freeSlot = &e;
  }
  if (!freeSlot) return false;
  freeSlot->guid = guid;
  freeSlot->clip = clip;
  freeSlot->used = true;
  return true;
}
const AudioClip* AudioSystem::findClip(const Guid& guid) {
  for (const Entry& e : g_clips)
    if (e.used && e.guid == guid) return &e.clip;
  return nullptr;
}
void AudioSystem::clearClips() {
  for (Entry& e : g_clips) e.used = false;
}

void AudioSource::Play() {
  AudioMixer* m = AudioSystem::mixer();
  const AudioClip* c = AudioSystem::findClip(clip);
  if (!m || !c) return;
  if (voice) m->stop(voice);
  voice = m->play(c, mute ? 0.0f : volume, panStereo, loop, pitch, prio(priority), groupOf(outputGroup));
  audioState_ = (audioState_ & ~2u) | (mute ? 2u : 0u);
}
void AudioSource::Stop() {
  if (AudioMixer* m = AudioSystem::mixer()) m->stop(voice);
  voice = 0;
}
void AudioSource::PlayOneShot(const Guid& other, Scalar volumeScale) {
  AudioMixer* m = AudioSystem::mixer();
  const AudioClip* c = AudioSystem::findClip(other);
  if (m && c) m->play(c, mute ? 0.0f : volume * volumeScale, panStereo, false, pitch, prio(priority), groupOf(outputGroup));
}
bool AudioSource::isPlaying() const {
  AudioMixer* m = AudioSystem::mixer();
  return m && voice && m->isPlaying(voice);
}

// F6.10: AudioSource.playOnAwake and live mute, once per fixed step after the animators (a source is not a Behaviour, so it has no Awake).
namespace {
void audioSourceStep(World& world) {
  world.forEach<AudioSource>([](AudioSource& a) {
    GameObject* go = a.gameObject();
    if (!go || !a.enabled() || !go->activeInHierarchy()) return;
    if (!(a.audioState_ & 1u)) {
      a.audioState_ |= 1u;
      if (a.playOnAwake && !a.clip.isNull()) a.Play();
    } else if (a.voice && ((a.audioState_ & 2u) != 0) != a.mute) {
      if (AudioMixer* m = AudioSystem::mixer()) m->setVolume(a.voice, a.mute ? 0.0f : a.volume);
      a.audioState_ = (a.audioState_ & ~2u) | (a.mute ? 2u : 0u);
    }
  });
}
[[maybe_unused]] const bool g_registered = AnimatorSystem::addStepHook(&audioSourceStep);
}  // namespace

}  // namespace rensga
