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
}  // namespace rensga
