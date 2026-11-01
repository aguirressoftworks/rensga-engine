// F6 contract stubs (C0) for API 8 animation and audio: Animator, animation events, AudioMixer groups, PlayClipAtPoint. Safe
// defaults until B1 implements them. The signatures are final (rensga/core/animator.h, rensga/Audio.h, components.h).
#include "rensga/Audio.h"
#include "rensga/core/animator.h"
#include "rensga/core/components.h"
#include "rensga/core/reflect_method.h"
#include "rensga/core/world.h"

namespace rensga {

// ---- Audio: mixer groups and PlayClipAtPoint (F6.10) ----
namespace {
Scalar g_groupVolume[kAudioGroupCount] = {1, 1, 1};
bool g_groupMuted[kAudioGroupCount] = {false, false, false};
// 10^(k/20) for k = -80 .. +6 dB
const Scalar kDbTable[87] = {0.0001, 0.0001122018, 0.0001258925, 0.0001412538, 0.0001584893, 0.0001778279, 0.0001995262, 0.0002238721, 0.0002511886, 0.0002818383, 0.0003162278, 0.0003548134, 0.0003981072, 0.0004466836, 0.0005011872, 0.0005623413, 0.0006309573, 0.0007079458, 0.0007943282, 0.0008912509, 0.001, 0.001122018, 0.001258925, 0.001412538, 0.001584893, 0.001778279, 0.001995262, 0.002238721, 0.002511886, 0.002818383, 0.003162278, 0.003548134, 0.003981072, 0.004466836, 0.005011872, 0.005623413, 0.006309573, 0.007079458, 0.007943282, 0.008912509, 0.01, 0.01122018, 0.01258925, 0.01412538, 0.01584893, 0.01778279, 0.01995262, 0.02238721, 0.02511886, 0.02818383, 0.03162278, 0.03548134, 0.03981072, 0.04466836, 0.05011872, 0.05623413, 0.06309573, 0.07079458, 0.07943282, 0.08912509, 0.1, 0.1122018, 0.1258925, 0.1412538, 0.1584893, 0.1778279, 0.1995262, 0.2238721, 0.2511886, 0.2818383, 0.3162278, 0.3548134, 0.3981072, 0.4466836, 0.5011872, 0.5623413, 0.6309573, 0.7079458, 0.7943282, 0.8912509, 1, 1.122018, 1.258925, 1.412538, 1.584893, 1.778279, 1.995262f};
}  // namespace

Scalar AudioMixerGroups::DbToLinear(Scalar db) {
  if (!(db > -80.0f)) return 0;
  if (db >= 6.0f) return kDbTable[86];
  const Scalar pos = db + 80.0f;
  const u32 i = static_cast<u32>(pos);
  const Scalar f = pos - static_cast<Scalar>(i);
  return kDbTable[i] + (kDbTable[i + 1] - kDbTable[i]) * f;
}
void AudioMixerGroups::Apply() {
  AudioMixer* m = AudioSystem::mixer();
  if (!m) return;
  for (u32 i = 0; i < kAudioGroupCount; ++i) m->setGroupVolume(static_cast<AudioGroup>(i), g_groupMuted[i] ? 0.0f : g_groupVolume[i]);
}
void AudioMixerGroups::SetVolume(AudioGroup g, Scalar v) {
  if (static_cast<u32>(g) >= kAudioGroupCount) return;
  g_groupVolume[static_cast<u32>(g)] = v < 0 ? 0 : (v > 2 ? 2 : v);
  Apply();
}
void AudioMixerGroups::SetVolumeDb(AudioGroup g, Scalar db) { SetVolume(g, DbToLinear(db)); }
Scalar AudioMixerGroups::GetVolume(AudioGroup g) { return static_cast<u32>(g) < kAudioGroupCount ? g_groupVolume[static_cast<u32>(g)] : 0; }
void AudioMixerGroups::SetMuted(AudioGroup g, bool m) {
  if (static_cast<u32>(g) >= kAudioGroupCount) return;
  g_groupMuted[static_cast<u32>(g)] = m;
  Apply();
}
bool AudioMixerGroups::IsMuted(AudioGroup g) { return static_cast<u32>(g) < kAudioGroupCount && g_groupMuted[static_cast<u32>(g)]; }
void AudioMixerGroups::ResetAll() {
  for (u32 i = 0; i < kAudioGroupCount; ++i) {
    g_groupVolume[i] = 1;
    g_groupMuted[i] = false;
  }
  Apply();
}

// Fire and forget at a world position: pan from the main camera's right vector, linear fall-off from 1 unit (full) to 40 units (silent).
bool AudioSource::PlayClipAtPoint(const Guid& clip, const Vector3& position, Scalar volume) {
  AudioMixer* m = AudioSystem::mixer();
  const AudioClip* c = AudioSystem::findClip(clip);
  if (!m || !c) return false;
  Scalar pan = 0, atten = 1;
  if (World* w = World::active()) {
    Camera* cam = nullptr;
    w->forEach<Camera>([&](Camera& cc) {
      if (!cam && cc.gameObject() && cc.gameObject()->activeInHierarchy()) cam = &cc;
    });
    if (cam) {
      const Vector3 cp = cam->transform()->position();
      const Vector3 d = position - cp;
      const Scalar dist = d.magnitude();
      constexpr Scalar kNear = 1.0f, kFar = 40.0f;
      if (dist >= kFar) return false;
      if (dist > kNear) atten = (kFar - dist) / (kFar - kNear);
      if (dist > 0.0001f) {
        pan = cam->transform()->right().x * d.x / dist + cam->transform()->right().y * d.y / dist + cam->transform()->right().z * d.z / dist;
        pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
      }
    }
  }
  return m->play(c, volume * atten, pan, false, 1.0f, 128, AudioGroup::SFX) != 0;
}

}  // namespace rensga
