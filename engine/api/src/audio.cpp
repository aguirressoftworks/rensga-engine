#include "rensga/Audio.h"
#include "rensga/core/components.h"

namespace rensga {
namespace {
AudioMixer* g_mixer = nullptr;
struct Entry { Guid guid; AudioClip clip; bool used = false; };
Entry g_clips[AudioSystem::kMaxClips];
u8 prio(i32 p) { return static_cast<u8>(p < 0 ? 0 : (p > 255 ? 255 : p)); }  // AudioSource.priority, F5.17
}  // namespace

void AudioSystem::setMixer(AudioMixer* mixer) { g_mixer = mixer; }
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
  voice = m->play(c, volume, panStereo, loop, pitch, prio(priority));
}
void AudioSource::Stop() {
  if (AudioMixer* m = AudioSystem::mixer()) m->stop(voice);
  voice = 0;
}
void AudioSource::PlayOneShot(const Guid& other, Scalar volumeScale) {
  AudioMixer* m = AudioSystem::mixer();
  const AudioClip* c = AudioSystem::findClip(other);
  if (m && c) m->play(c, volume * volumeScale, panStereo, false, pitch, prio(priority));
}
bool AudioSource::isPlaying() const {
  AudioMixer* m = AudioSystem::mixer();
  return m && voice && m->isPlaying(voice);
}
}  // namespace rensga
