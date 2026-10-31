// Built-in components. Registered through reflection like any script.
// Physics (CharacterController::Move, trigger detection) lives in engine/api/src/physics.cpp.
#pragma once
#include "rensga/core/behaviour_events.h"
#include "rensga/core/feature.h"
#include "rensga/core/material_block.h"
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
  // F6 (B2): per-renderer color / texture override without duplicating the material. Runtime only; copied by Instantiate.
  MaterialPropertyBlock propertyBlock_;
  void SetPropertyBlock(const MaterialPropertyBlock& block) { propertyBlock_ = block; }
  const MaterialPropertyBlock& GetPropertyBlock() const { return propertyBlock_; }
  void ClearPropertyBlock() { propertyBlock_.Clear(); }
  void OnCloned(const Component* source) override {
    meshIndex = static_cast<const MeshRenderer*>(source)->meshIndex;
    propertyBlock_ = static_cast<const MeshRenderer*>(source)->propertyBlock_;
  }
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
  // F6 (appended).
  APPENDED_FIELDS()
  SERIALIZE_FIELD(Vector3, center, Vector3(0, 0, 0))
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
  // F6 (appended; Camera API, implemented by B2). A scene saved before F6 keeps rendering exactly as before: these defaults are
  // the old behaviour (one perspective camera, full screen, solid clear color).
  APPENDED_FIELDS()
  SERIALIZE_FIELD(bool, orthographic, false)
  TOOLTIP("Half of the vertical view size in world units (orthographic only).")
  SERIALIZE_FIELD(Scalar, orthographicSize, 5.0f)
  TOOLTIP("Normalized view rectangle on the screen: x, y from the bottom-left, width, height (Unity's Camera.rect).")
  RANGE(0, 1)
  SERIALIZE_FIELD(Scalar, viewportX, 0.0f)
  RANGE(0, 1)
  SERIALIZE_FIELD(Scalar, viewportY, 0.0f)
  RANGE(0, 1)
  SERIALIZE_FIELD(Scalar, viewportW, 1.0f)
  RANGE(0, 1)
  SERIALIZE_FIELD(Scalar, viewportH, 1.0f)
  TOOLTIP("Cameras draw in increasing depth; the main camera is the enabled one with the lowest depth.")
  SERIALIZE_FIELD(Scalar, depth, 0.0f)
  SERIALIZE_ENUM(clearFlags, 1, "Skybox|SolidColor|DepthOnly|Nothing")
  SERIALIZE_FIELD(Color32, backgroundColor, Color32(49, 77, 121, 255))
  TOOLTIP("Bit n set = objects on layer n are drawn. -1 = every layer.")
  SERIALIZE_FIELD(i32, cullingMask, -1)

  // ---- Unity API (F6, defined in engine/api/src/render_api.cpp; stubs returning zeros until B2). Screen points are in the
  // renderer's virtual pixels with the origin at the TOP-left (like the UI); z is the distance from the camera in world units.
  // Viewport points are 0..1 with the origin at the BOTTOM-left of the camera's rect (Unity). Needs the camera's object to be
  // in a World; a null result means "no main camera".
  static Camera* main();  // the enabled, active Camera with the lowest depth (null if none)
  Vector3 ScreenToWorldPoint(const Vector3& screenPoint) const;
  Vector3 WorldToScreenPoint(const Vector3& worldPoint) const;
  Ray ScreenPointToRay(const Vector2& screenPoint) const;
  Vector3 WorldToViewportPoint(const Vector3& worldPoint) const;
  Vector3 ViewportToWorldPoint(const Vector3& viewportPoint) const;
  Ray ViewportPointToRay(const Vector2& viewportPoint) const;
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
  // F6 (appended; B1). playOnAwake is OFF by default, unlike Unity: a scene made before F6 has AudioSources that were started by
  // scripts, and they must stay silent until played.
  APPENDED_FIELDS()
  TOOLTIP("Start playing when the object wakes up.")
  SERIALIZE_FIELD(bool, playOnAwake, false)
  SERIALIZE_FIELD(bool, mute, false)
  TOOLTIP("The mixer group whose volume scales this source (AudioMixer::SetGroupVolume).")
  SERIALIZE_ENUM(outputGroup, 0, "Master|Music|SFX")
  // Runtime only (not reflected): the mixer voice of the last Play().
  u32 voice = 0;
  void Play();                       // (re)starts the clip on this source
  void Stop();
  void PlayOneShot(const Guid& other, Scalar volumeScale = 1.0f);  // fire and forget; does not touch `voice`
  // F6 (B1): fire and forget at a world position (pan and distance attenuation from the main camera); no AudioSource needed.
  // Defined in engine/api/src/animation_api.cpp (stub: returns false, plays nothing, until B1).
  static bool PlayClipAtPoint(const Guid& clip, const Vector3& position, Scalar volume = 1.0f);
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
  MaterialPropertyBlock propertyBlock_;  // F6 (B2), see MeshRenderer
  void SetPropertyBlock(const MaterialPropertyBlock& block) { propertyBlock_ = block; }
  const MaterialPropertyBlock& GetPropertyBlock() const { return propertyBlock_; }
  void ClearPropertyBlock() { propertyBlock_.Clear(); }
  void OnCloned(const Component* source) override {
    meshIndex = static_cast<const SkinnedMeshRenderer*>(source)->meshIndex;
    propertyBlock_ = static_cast<const SkinnedMeshRenderer*>(source)->propertyBlock_;
  }
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
