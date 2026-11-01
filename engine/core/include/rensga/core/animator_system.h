// F6.03 / F6.10 (B1): the Animator runtime. A controller is an ANIC record of the pack; every Animator component owns a slot of a
// fixed table (no heap) with its parameters, current state, optional next state (cross-fade) and times in integer Q16 units.
// Property-clip states write reflected fields through PropClips::apply; skeletal (CLP3) states build a palette in a fixed pool that
// AnimationSystem::palette hands to the renderer. One call per fixed step (AnimationSystem::fixedStep calls fixedStep()).
#pragma once
#include "rensga/core/animator.h"
#include "rensga/core/components.h"
#include "rensga/core/world.h"

namespace rensga {
namespace AnimatorSystem {

constexpr u32 kMaxAnimators = 32;     // live Animator components with a controller
constexpr u32 kMaxParams = 16;        // parameters per controller (the cooker refuses more)
constexpr u32 kPalettePool = 512;     // matrices for skeletal states of all animators (32 KB)

void fixedStep(World& world);
void shutdown();                      // forgets every slot (tests, pack change)
void setTrace(bool on);               // "rensga: anim f=<step> h=<hash>" per fixed step while an animator exists
u32 stepCount();
u32 stateHash();                      // FNV-1a over every slot's state, times, parameters and the property values written in the last step

struct DebugInfo {
  bool valid = false;
  u32 current = 0;                    // state index in the controller
  u32 next = kInvalidIndex;           // state index being blended in, or kInvalidIndex
  Scalar normalizedTime = 0;          // of the current state
  Scalar blend = 0;                   // 0..1 progress of the cross-fade
  u32 stateCount = 0;
};
bool debugInfo(const Animator& animator, DebugInfo& out);

// The palette of a skinned object whose Animator plays a skeletal state (null otherwise).
const Matrix4* palette(const World& world, const SkinnedMeshRenderer& smr, u32* count);

// Other systems run after the animators in the same fixed step (the API registers AudioSource.playOnAwake here).
typedef void (*StepHook)(World& world);
bool addStepHook(StepHook hook);

}  // namespace AnimatorSystem
}  // namespace rensga
