// F5 (PSP Engine 6): skeletal clip player. Time is an integer tick count on the fixed step, so Linux, the PSP simulator and
// the console agree bit for bit (RULES 1.9). Poses are nlerp between the cooker-resampled frames. Implemented in
// engine/core/src/animation.cpp. Nothing here allocates per frame: the palette pool is sized once at load from an arena.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/components.h"
#include "rensga/core/memory.h"
#include "rensga/core/pack_format.h"
#include "rensga/core/world.h"

namespace rensga {

// Samples `clip` at `tick` fixed steps (tickHz = fixed steps per second). Clip frame = tick * fps / tickHz (integer
// division); the fractional part blends frame and frame + 1 (nlerp of rotation, lerp of translation and scale). A looping
// clip wraps, a one-shot clip holds its last frame. `poses` is clip.frameCount x jointCount PackJointPose, `out` has jointCount.
void sampleClip(const PackClip3& clip, const PackJointPose* poses, u32 jointCount, u32 tick, u32 tickHz, PackJointPose* out);

// Model-space matrices for one pose: palette[i] = jointModel[i] * invBind[i] (SKIN joint order, parents first).
void buildPalette(const PackSkin& skin, const PackJoint* joints, const PackJointPose* pose, Matrix4* palette);

namespace AnimationSystem {
// Sizes the palette pool for up to `maxMatrices` matrices in total from the arena (startup only). False if it does not fit.
bool init(Arena& arena, const PackAssets& assets, u32 maxMatrices);
void shutdown();
// Advances every playing Animation by one fixed step and rebuilds its palette. Called once per fixed step by the loop.
void fixedStep(World& world);
// The palette of a SkinnedMeshRenderer's object for this frame (null when its Animation is absent: bind pose is then used).
// `count` receives the matrix count.
const Matrix4* palette(const World& world, const SkinnedMeshRenderer& smr, u32* count);
// FNV-1a over every palette's float bits, printed as "rensga: skinhash=%08x" by the headless / PSP mains.
u32 skinHash();
// Debug / determinism: with trace on, every fixed step logs "rensga: skin f=<step> h=<skinHash>". Off by default.
void setTrace(bool on);
// Number of fixed steps AnimationSystem has run since the last shutdown.
u32 stepCount();
}  // namespace AnimationSystem

}  // namespace rensga
