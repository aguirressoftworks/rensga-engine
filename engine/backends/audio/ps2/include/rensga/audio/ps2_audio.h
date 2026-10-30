// PS2 audio backend: audsrv (IOP audio server) fed from the software mixer on the main thread.
// The IOP side streams the PCM16 ring to the SPU2. Call init() after Ps2Platform::init() (it has
// reset the IOP and started SIF RPC). The audsrv IRX is embedded in the ELF (no file needed on
// the disc), LIBSD comes from the BIOS ROM (rom0:LIBSD).
#pragma once
#include "rensga/core/audio.h"
#include "rensga/core/backends.h"

namespace rensga {

class Ps2Audio : public IAudioBackend {
 public:
  bool init(u32 sampleRate = 22050, AudioMixer* mixer = nullptr) override;
  void shutdown() override;
  void update() override;
  u32 sampleRate() const override { return rate_; }

 private:
  AudioMixer* mixer_ = nullptr;
  u32 rate_ = 0;
};

}  // namespace rensga
