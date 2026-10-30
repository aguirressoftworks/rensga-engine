// Engine 4 glue shared by the desktop/Android player and the PS2 main: one AudioMixer plus the platform audio
// backend (SDL on Linux/Windows/Android, audsrv on PS2), sized from the ConsoleTarget (CTGT audioSampleRate / audioVoices).
// Pack clips (AUDI) are registered with AudioSystem so AudioSource components play through the mixer. Music is never
// loaded whole: AudioMixer::playMusic streams data:/music.wav in double-buffered chunks through IFileSystem.
// Where music.wav comes from: <project>/Streaming/music.wav is copied by the cooker next to game.rpak (the data root);
// the PS2 ISO script puts it on the disc root as MUSIC.WAV (cdrom0:\MUSIC.WAV;1); the APK ships it in assets/.
#pragma once
#include "rensga/core/audio.h"
#include "rensga/core/backends.h"
#include "rensga/core/pack_format.h"

namespace rensga {

class RuntimeAudio {
 public:
  static constexpr const char* kMusicPath = "data:/music.wav";

  // `ct` may be null (no pack): defaults 22050 Hz, 16 voices. `clock` returns microseconds (for the stress numbers).
  // A failing device is not fatal: the game runs silent and init returns false.
  bool init(IFileSystem* fs, const PackConsoleTarget* ct, AudioMixer::ClockUsFn clock);
  void shutdown();
  // Once per frame, after the game update: refills music chunks and keeps the device fed.
  void update();

  void registerPackClips(const PackAssets& assets);  // AUDI -> AudioSystem::registerClip
  bool startMusic(Scalar volume = 0.6f);             // false when data:/music.wav is missing (not an error)

  // --audio-stress: looping streamed music + 16 overlapping looping effects (pack clips if any, else built-in tones).
  bool beginStress(const PackAssets* assets);
  void logStress(u32 frames);  // "rensga: audio-stress voices=16 mix_us=<avg>/<max> ..."

  AudioMixer& mixer() { return mixer_; }
  bool deviceOk() const { return deviceOk_; }

 private:
  AudioMixer mixer_;
  IAudioBackend* backend_ = nullptr;
  bool deviceOk_ = false;
  bool musicOn_ = false;
};

}  // namespace rensga
