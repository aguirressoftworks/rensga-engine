// F5: stub. Agent A2 implements the sampler, the palette builder and AnimationSystem (see animation.h).
#include "rensga/core/animation.h"

namespace rensga {

void sampleClip(const PackClip3&, const PackJointPose*, u32, u32, u32, PackJointPose*) {}
void buildPalette(const PackSkin&, const PackJoint*, const PackJointPose*, Matrix4*) {}

bool AnimationSystem::init(Arena&, const PackAssets&, u32) { return true; }
void AnimationSystem::shutdown() {}
void AnimationSystem::fixedStep(World&) {}
const Matrix4* AnimationSystem::palette(const World&, const SkinnedMeshRenderer&, u32* count) {
  if (count) *count = 0;
  return nullptr;
}
u32 AnimationSystem::skinHash() { return 0; }

void Animation::Play() { playing = true; tick = 0; }
void Animation::Stop() { playing = false; }
bool Animation::isPlaying() const { return playing; }

}  // namespace rensga
