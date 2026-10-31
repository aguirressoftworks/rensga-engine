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

// F6 contract (C0): Unity's AudioMixer groups, fixed to Master, Music and SFX (AudioSource::outputGroup picks the group). Volumes
// are linear 0..2, default 1. Stub until B1 (Stage 2, API 8): the values are stored and returned, but the mixer ignores them.
// Defined in engine/api/src/animation_api.cpp.
struct AudioMixerGroups {
  static void SetVolume(AudioGroup group, Scalar linear);
  static Scalar GetVolume(AudioGroup group);
  static void SetMuted(AudioGroup group, bool muted);
  static bool IsMuted(AudioGroup group);
  static void ResetAll();  // every group back to volume 1, unmuted
};
}  // namespace rensga
