// RuntimeAudio without a device, for the simulation mains (rensga_headless, rensga_sim): no SDL, nothing is mixed, no state is
// kept. bootPack links against RuntimeAudio::init / registerPackClips even when BootDeps::audio is null; this satisfies the
// linker without dragging a backend in. A main that wants sound compiles runtime_audio.cpp instead.
#include "runtime_audio.h"

namespace rensga {

bool RuntimeAudio::init(IFileSystem*, const PackConsoleTarget*, AudioMixer::ClockUsFn) { return false; }
void RuntimeAudio::shutdown() {}
void RuntimeAudio::update() {}
void RuntimeAudio::registerPackClips(const PackAssets&) {}
bool RuntimeAudio::startMusic(Scalar) { return false; }
bool RuntimeAudio::beginStress(const PackAssets*) { return false; }
void RuntimeAudio::logStress(u32) {}

}  // namespace rensga
