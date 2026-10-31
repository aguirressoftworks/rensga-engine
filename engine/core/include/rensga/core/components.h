// Built-in components. Registered through reflection like any script.
// Physics (CharacterController::Move, trigger detection) lives in engine/api/src/physics.cpp.
#pragma once
#include "rensga/core/feature.h"
#include "rensga/core/object.h"

namespace rensga {

// Forces this translation unit's registrars to be linked and runs them. Called by World::init.
void registerBuiltinComponents();

struct MeshRenderer : Component {
  REFLECT(MeshRenderer)
  SERIALIZE_FIELD(Guid, mesh, Guid())
  SERIALIZE_FIELD(Color32, color, Color32(255, 255, 255, 255))
  // F5 (appended: older scenes keep working). A Material asset (.mat.json): texture, color, unlit, alphaTest. A mesh with no
  // material stays baked (light baked into vertex colors) exactly as before; with one it is lit by the scene's Light components.
  SERIALIZE_FIELD(Guid, material, Guid())
  // Runtime only (not reflected): index into the pack MESH table, set by the pack loader.
  u32 meshIndex = kInvalidIndex;
  void OnCloned(const Component* source) override { meshIndex = static_cast<const MeshRenderer*>(source)->meshIndex; }
};

struct BoxCollider : Component {
  REFLECT(BoxCollider)
  SERIALIZE_FIELD(Vector3, center, Vector3(0, 0, 0))
  SERIALIZE_FIELD(Vector3, size, Vector3(1, 1, 1))
  SERIALIZE_FIELD(bool, isTrigger, false)
};

struct SphereCollider : Component {
  REFLECT(SphereCollider)
  SERIALIZE_FIELD(Scalar, radius, 0.5f)
  SERIALIZE_FIELD(bool, isTrigger, false)
};

struct CharacterController : Component {
  REFLECT(CharacterController)
  SERIALIZE_FIELD(Scalar, height, 2.0f)
  SERIALIZE_FIELD(Scalar, radius, 0.5f)
  SERIALIZE_FIELD(Scalar, gravity, -20.0f)
  SERIALIZE_FIELD(Scalar, stepOffset, 0.3f)
  // Runtime state (not reflected).
  Vector3 velocity;
  bool grounded = false;
  // Moves by `motion` (world space) with gravity and AABB collision. Implemented in engine/api.
  void Move(const Vector3& motion);
  bool isGrounded() const { return grounded; }
};

struct Camera : Component {
  REFLECT(Camera)
  SERIALIZE_FIELD(Scalar, fov, 60.0f)
  SERIALIZE_FIELD(Scalar, nearClip, 0.3f)
  SERIALIZE_FIELD(Scalar, farClip, 200.0f)
};

// The cooker bakes this light into vertex colors; the runtime does not light at all.
struct DirectionalLight : Component {
  REFLECT(DirectionalLight)
  SERIALIZE_FIELD(Vector3, direction, Vector3(-0.4f, -1.0f, -0.3f))
  SERIALIZE_FIELD(Color32, color, Color32(255, 255, 255, 255))
};

// Plays an AudioClip through the engine mixer (Engine 4). The clip is a GUID into the clip table
// (AudioSystem::registerClip, filled by the pack loader from AUDI). Implemented in engine/api/src/audio.cpp.
// Not required in v1: spatialBlend (no 3D attenuation). pitch is supported by the mixer.
struct AudioSource : Component {
  REFLECT(AudioSource)
  SERIALIZE_FIELD(Guid, clip, Guid())
  SERIALIZE_FIELD(Scalar, volume, 1.0f)
  SERIALIZE_FIELD(Scalar, pitch, 1.0f)
  SERIALIZE_FIELD(Scalar, panStereo, 0.0f)
  SERIALIZE_FIELD(bool, loop, false)
  // F5.17 (appended): Unity's priority, 0 = most important .. 255 = least; the mixer steals the least important voice first.
  SERIALIZE_FIELD(i32, priority, 128)
  // Runtime only (not reflected): the mixer voice of the last Play().
  u32 voice = 0;
  void Play();                       // (re)starts the clip on this source
  void Stop();
  void PlayOneShot(const Guid& other, Scalar volumeScale = 1.0f);  // fire and forget; does not touch `voice`
  bool isPlaying() const;
};

// F5 shared 3D. A Light is lit at run time by the renderer (a scene with no Light and a DirectionalLight stays baked).
// type: 0 = directional (the light travels along Transform::forward() of its object), 1 = point (at the object's position).
// renderMode: 0 = Auto, 1 = Important (always among the object's lights), 2 = NotImportant.
struct Light : Component {
  REFLECT(Light)
  TOOLTIP("0 = Directional (shines along the object's forward axis), 1 = Point.")
  RANGE(0, 1)
  SERIALIZE_FIELD(i32, type, 0)
  SERIALIZE_FIELD(Color32, color, Color32(255, 255, 255, 255))
  RANGE(0, 8)
  SERIALIZE_FIELD(Scalar, intensity, 1.0f)
  TOOLTIP("Point lights: distance where the light reaches zero.")
  SERIALIZE_FIELD(Scalar, range, 10.0f)
  TOOLTIP("0 = Auto, 1 = Important (always lights nearby objects), 2 = NotImportant.")
  RANGE(0, 2)
  SERIALIZE_FIELD(i32, renderMode, 0)
};

// Scene-wide lighting and fog (Unity: RenderSettings). The first active one is used.
struct RenderSettings : Component {
  REFLECT(RenderSettings)
  SERIALIZE_FIELD(Color32, ambient, Color32(64, 64, 64, 255))
  SERIALIZE_FIELD(bool, fogEnabled, false)
  SERIALIZE_FIELD(Color32, fogColor, Color32(128, 128, 128, 255))
  SERIALIZE_FIELD(Scalar, fogStart, 20.0f)
  SERIALIZE_FIELD(Scalar, fogEnd, 100.0f)
};

// A skinned mesh (glTF with a skin). `mesh` is the glTF asset, `material` a .mat.json. An Animation on the same object drives it.
struct SkinnedMeshRenderer : Component {
  REFLECT(SkinnedMeshRenderer)
  SERIALIZE_FIELD(Guid, mesh, Guid())
  SERIALIZE_FIELD(Guid, material, Guid())
  // Runtime only (not reflected): index into the pack MESH / MSHL tables, set by the pack loader.
  u32 meshIndex = kInvalidIndex;
  void OnCloned(const Component* source) override { meshIndex = static_cast<const SkinnedMeshRenderer*>(source)->meshIndex; }
};

// Unity's legacy Animation component: plays one skeletal clip (a CLP3 in the pack) on the SkinnedMeshRenderer of the same object.
// Time is an integer tick count on the fixed step (animation.h). wrapMode: 0 = Default (the clip's own loop flag), 1 = Once, 2 = Loop.
struct Animation : Component {
  REFLECT(Animation)
  SERIALIZE_FIELD(Guid, clip, Guid())
  SERIALIZE_FIELD(bool, playAutomatically, true)
  RANGE(0, 2)
  SERIALIZE_FIELD(i32, wrapMode, 0)
  SERIALIZE_FIELD(Scalar, speed, 1.0f)
  // Runtime only (not reflected); the animation system owns them.
  bool playing = false;
  u32 tick = 0;            // fixed steps since Play()
  u32 phase = 0;           // fractional carry for speed != 1 (animation system units)
  u32 paletteSlot = kInvalidIndex;
  // F5.12: skeletal animation needs the "skinning" feature of the profile.
  RENSGA_REQUIRES_FEATURE(SKINNING, "Skeletal animation is not available on this profile; use sprite or vertex animation, or add \"skinning\" to ConsoleTarget.json features.")
  void Play();             // restarts the clip from its first frame
  RENSGA_REQUIRES_FEATURE(SKINNING, "Skeletal animation is not available on this profile; use sprite or vertex animation, or add \"skinning\" to ConsoleTarget.json features.")
  void Stop();
  bool isPlaying() const;
};

}  // namespace rensga
