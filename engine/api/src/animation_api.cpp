// F6 contract stubs (C0) for API 8 animation and audio: Animator, animation events, AudioMixer groups, PlayClipAtPoint. Safe
// defaults until B1 implements them. The signatures are final (rensga/core/animator.h, rensga/Audio.h, components.h).
#include "rensga/Audio.h"
#include "rensga/core/animator.h"
#include "rensga/core/components.h"
#include "rensga/core/reflect_method.h"

namespace rensga {

// ---- Animator (TODO(B1 animation): ANIC state machine, PCLP property clips, CLP3 skeletal states, integer ticks) ----
bool AnimatorStateInfo::IsName(const char* name) const { return name && nameHash == fnv1a32(name); }
u32 Animator::StringToHash(const char* name) { return name ? fnv1a32(name) : 0; }
void Animator::SetFloat(const char*, Scalar) {}
void Animator::SetInt(const char*, i32) {}
void Animator::SetBool(const char*, bool) {}
void Animator::SetTrigger(const char*) {}
void Animator::ResetTrigger(const char*) {}
void Animator::SetFloat(u32, Scalar) {}
void Animator::SetInt(u32, i32) {}
void Animator::SetBool(u32, bool) {}
void Animator::SetTrigger(u32) {}
void Animator::ResetTrigger(u32) {}
Scalar Animator::GetFloat(const char*) const { return 0; }
i32 Animator::GetInt(const char*) const { return 0; }
bool Animator::GetBool(const char*) const { return false; }
bool Animator::Play(const char*) { return false; }
bool Animator::Play(u32) { return false; }
bool Animator::CrossFade(const char*, Scalar) { return false; }
AnimatorStateInfo Animator::GetCurrentStateInfo() const { return AnimatorStateInfo(); }

namespace AnimationEvents {
namespace {
Handler g_handler = nullptr;
}
void setHandler(Handler handler) { g_handler = handler; }
// TODO(B1 animation): default handler = OnAnimationEvent on every Behaviour of `target`, then the METHOD(Name) method by name.
void dispatch(GameObject* target, const AnimationEvent& event) {
  if (g_handler) g_handler(target, event);
}
}  // namespace AnimationEvents

// ---- Audio (TODO(B1 audio): wire the groups into AudioMixer, position for PlayClipAtPoint) ----
namespace {
Scalar g_groupVolume[kAudioGroupCount] = {1, 1, 1};
bool g_groupMuted[kAudioGroupCount] = {false, false, false};
}  // namespace
void AudioMixerGroups::SetVolume(AudioGroup g, Scalar v) {
  if (static_cast<u32>(g) < kAudioGroupCount) g_groupVolume[static_cast<u32>(g)] = v < 0 ? 0 : (v > 2 ? 2 : v);
}
Scalar AudioMixerGroups::GetVolume(AudioGroup g) { return static_cast<u32>(g) < kAudioGroupCount ? g_groupVolume[static_cast<u32>(g)] : 0; }
void AudioMixerGroups::SetMuted(AudioGroup g, bool m) {
  if (static_cast<u32>(g) < kAudioGroupCount) g_groupMuted[static_cast<u32>(g)] = m;
}
bool AudioMixerGroups::IsMuted(AudioGroup g) { return static_cast<u32>(g) < kAudioGroupCount && g_groupMuted[static_cast<u32>(g)]; }
void AudioMixerGroups::ResetAll() {
  for (u32 i = 0; i < kAudioGroupCount; ++i) {
    g_groupVolume[i] = 1;
    g_groupMuted[i] = false;
  }
}
bool AudioSource::PlayClipAtPoint(const Guid&, const Vector3&, Scalar) { return false; }

}  // namespace rensga
