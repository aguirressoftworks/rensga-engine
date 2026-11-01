// Engine 4 script-facing audio: AudioSource (rensga/core/components.h) plays clips that are
// registered here. Capacity is fixed (no heap). One mixer per process.
#pragma once
#include "rensga/core/audio.h"
#include "rensga/core/components.h"
#include "rensga/core/guid.h"

namespace rensga {
struct AudioSystem {
  static constexpr unsigned kMaxClips = 64;
  // The mixer the AudioSource components play through (owned by the runtime main). Null = silent.
  static void setMixer(AudioMixer* mixer);
  static AudioMixer* mixer();
  // Clips are borrowed: the samples must outlive the registration (they live in the pack).
  static bool registerClip(const Guid& guid, const AudioClip& clip);
  static const AudioClip* findClip(const Guid& guid);
  static void clearClips();
};

// F6.10 (B1): Unity's AudioMixer groups, fixed to Master, Music and SFX. AudioSource::outputGroup picks the group of a source; the
// streamed music voice is in Music. Volumes are LINEAR, 0..2, default 1 (SetVolumeDb takes decibels: 0 dB = 1, -6 dB = 0.5, -80 dB
// and below = 0, up to +6 dB = 2; the conversion is a table, no libm). A muted group plays at 0 without losing its volume. The values
// live here and reach the mixer at once (and again whenever AudioSystem::setMixer binds a mixer), so a menu can set them before the
// game starts. Defined in engine/api/src/animation_api.cpp.
struct AudioMixerGroups {
  static void SetVolume(AudioGroup group, Scalar linear);
  static void SetVolumeDb(AudioGroup group, Scalar decibels);
  static Scalar GetVolume(AudioGroup group);
  static void SetMuted(AudioGroup group, bool muted);
  static bool IsMuted(AudioGroup group);
  static void ResetAll();  // every group back to volume 1, unmuted
  static Scalar DbToLinear(Scalar decibels);
  static void Apply();     // pushes the stored values to the bound mixer (AudioSystem::setMixer calls it)
};
}  // namespace rensga
