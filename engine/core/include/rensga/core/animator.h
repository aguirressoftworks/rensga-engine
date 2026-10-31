// F6 contract (C0): the Animator component and the animation API types. Fields are final; the runtime is stubbed until B1
// (Stage 2, API 8): an Animator loads, cooks and round-trips and plays nothing. Methods are defined in
// engine/api/src/animation_api.cpp (stubs return safe defaults).
//
// The controller is an ANIC record of the pack (states, transitions, conditions, parameters; see pack-format.md). State clips
// are PCLP property clips (any reflected field by path, so sprites, transforms and colors all animate) or F5 CLP3 skeletal clips.
// Time is an integer tick count on the fixed step, so the state is deterministic on every profile.
#pragma once
#include "rensga/core/object.h"

namespace rensga {

struct AnimatorStateInfo {
  u32 nameHash = 0;          // Animator::StringToHash(state name)
  Scalar normalizedTime = 0;  // 0..1 per loop; the integer part counts the loops
  Scalar length = 0;          // seconds of the state's clip at speed 1
  bool IsName(const char* name) const;
};

struct Animator : Component {
  REFLECT(Animator)
  TOOLTIP("AnimatorController asset (ANIC in the pack).")
  SERIALIZE_FIELD(Guid, controller, Guid())
  SERIALIZE_FIELD(Scalar, speed, 1.0f)
  TOOLTIP("Not simulated in F6 (root motion needs the skeleton's root bone).")
  SERIALIZE_FIELD(bool, applyRootMotion, false)

  // Runtime state (not reflected), owned by the animator system (B1).
  u32 runtimeIndex_ = kInvalidIndex;

  // Unity's names. Parameters are found by name or by the hash from StringToHash (FNV-1a 32 of the name).
  static u32 StringToHash(const char* name);
  void SetFloat(const char* name, Scalar value);
  void SetInt(const char* name, i32 value);
  void SetBool(const char* name, bool value);
  void SetTrigger(const char* name);
  void ResetTrigger(const char* name);
  void SetFloat(u32 hash, Scalar value);
  void SetInt(u32 hash, i32 value);
  void SetBool(u32 hash, bool value);
  void SetTrigger(u32 hash);
  void ResetTrigger(u32 hash);
  Scalar GetFloat(const char* name) const;
  i32 GetInt(const char* name) const;
  bool GetBool(const char* name) const;
  // Jumps to a state (no blend) / cross-fades to it over `durationSeconds`. False when the state does not exist.
  bool Play(const char* stateName);
  bool Play(u32 stateHash);
  bool CrossFade(const char* stateName, Scalar durationSeconds);
  AnimatorStateInfo GetCurrentStateInfo() const;
};

// Where an AnimationEvent goes. The animator system calls dispatch(); the default handler sends Behaviour::OnAnimationEvent to
// every Behaviour on the object and then calls the METHOD(Name) method called `functionName` (void(), or one Scalar / i32
// argument taken from floatParameter / intParameter). setHandler replaces the default (tests, editor).
namespace AnimationEvents {
using Handler = void (*)(GameObject* target, const AnimationEvent& event);
void setHandler(Handler handler);  // null restores the default
void dispatch(GameObject* target, const AnimationEvent& event);
}  // namespace AnimationEvents

}  // namespace rensga
