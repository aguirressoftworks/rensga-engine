// MVP-18/29, F3.06: input over the VirtualGamepad. Two APIs share one set of actions:
//   Classic (Unity's Input class): GetAxis / GetAxisRaw / GetButton / GetButtonDown / GetButtonUp by action name,
//     GetKey / GetKeyDown / GetKeyUp(KeyCode), anyKey.
//   Actions (Unity's Input System): InputAction* jump = InputActions::Find("Jump") once, then
//     jump->WasPressedThisFrame(), IsPressed(), ReadValue<Scalar>() / ReadValue<Vector2>() with no string lookup per frame.
// Actions come from the pack (cooked from ProjectSettings/InputActions.json) or are defined in code. Sticks get a radial
// dead zone and an outer clamp here (defaults 0.2 / 0.95, "stickDeadZone" / "stickOuterClamp" in the JSON), so platforms
// report raw stick values. Keys exist only where the target has a keyboard; elsewhere GetKey is always false.
// Rumble: Input::SetVibration is a no-op unless the ConsoleTarget says "capabilities": {"rumble": true}.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/keycode.h"
#include "rensga/core/pack_format.h"

namespace rensga {

// Everything needed to define an action in code (loadActions fills the same thing from the pack).
// bind[] and the meaning of `source` follow PackInputActionExt in pack_format.h.
struct InputActionDef {
  const char* name = "";
  PackInputKind kind = PackInputKind::Button;
  u32 source = static_cast<u32>(PackInputSource::South);  // a PackInputSource, or kInputKeyBase + KeyCode for a key button
  u32 bind[8] = {kInputNone, kInputNone, kInputNone, kInputNone, kInputNone, kInputNone, kInputNone, kInputNone};
  Scalar gravity = 3, sensitivity = 3, dead = 0;  // keyboard ramp speeds in units per second (Unity's defaults)
  u32 flags = 0;                                  // kInputFlagSnap | kInputFlagInvert
};

class Input;
struct InputImpl;

class InputAction {
 public:
  static constexpr u32 kMaxNameLength = 24;
  const char* name() const { return name_; }
  PackInputKind kind() const { return kind_; }
  bool WasPressedThisFrame() const { return pressed_ && !prevPressed_; }
  bool WasReleasedThisFrame() const { return !pressed_ && prevPressed_; }
  // Buttons: held. Axes: actuated (value 0.5 or more away from 0).
  bool IsPressed() const { return pressed_; }
  template <class T>
  T ReadValue() const;  // Scalar (first axis), Vector2 (y up) or bool

 private:
  friend class Input;
  friend struct InputImpl;
  char name_[kMaxNameLength] = {};
  PackInputKind kind_ = PackInputKind::Button;
  u32 source_ = 0;
  u32 bind_[8] = {kInputNone, kInputNone, kInputNone, kInputNone, kInputNone, kInputNone, kInputNone, kInputNone};
  Scalar gravity_ = 3, sensitivity_ = 3, dead_ = 0;
  u32 flags_ = 0;
  Vector2 value_, raw_, ramp_;  // value_ is what GetAxis returns, raw_ is GetAxisRaw
  bool pressed_ = false, prevPressed_ = false;
};
template <>
inline Scalar InputAction::ReadValue<Scalar>() const { return value_.x; }
template <>
inline Vector2 InputAction::ReadValue<Vector2>() const { return value_; }
template <>
inline bool InputAction::ReadValue<bool>() const { return pressed_; }

class Input {
 public:
  static constexpr u32 kMaxActions = 32;
  static constexpr u32 kMaxNameLength = InputAction::kMaxNameLength;

  // ---- setup (the loop and the runtime call these; scripts don't) ----
  // Clears all actions and loads the pack's INPT (+ INPX, INPS) tables and the CTGT capabilities.
  static void loadActions(const PackView& pack, const PackInputAction* actions, u32 count);
  static InputAction* define(const InputActionDef& def);  // nullptr when the table is full
  static void defineButton(const char* name, PackInputSource source);
  static void defineAxis2(const char* name, PackInputSource source);
  static void defineAxis(const char* name, PackInputSource source);
  static void clearActions();
  static void setStickSettings(Scalar deadZone, Scalar outerClamp);
  static Scalar stickDeadZone();
  static Scalar stickOuterClamp();
  // Called once per frame by GameLoop with the freshly polled pad. `dt` drives the keyboard ramp of GetAxis.
  static void update(const VirtualGamepad& pad, Scalar dt = 1.0f / 60.0f);

  // ---- Classic API ----
  // First axis of the action (x of an Axis2; 0 or 1 for a button). 0 if the action is unknown.
  static Scalar GetAxis(const char* name);
  static Scalar GetAxisRaw(const char* name);  // keyboard bindings give -1, 0 or 1 at once instead of ramping
  static Vector2 GetAxis2(const char* name);   // Move -> (x, y), y up
  static bool GetButton(const char* name);
  static bool GetButtonDown(const char* name);  // went down this frame
  static bool GetButtonUp(const char* name);    // went up this frame
  static bool GetKey(KeyCode key);
  static bool GetKeyDown(KeyCode key);
  static bool GetKeyUp(KeyCode key);
  static bool anyKey();      // any key or pad button held
  static bool anyKeyDown();  // any key or pad button went down this frame
  static Vector2 GetRightStick();             // (rightX, rightY), y up
  static Scalar GetTrigger(bool right);       // L2 / R2, 0..1
  static const VirtualGamepad& pad();         // held / pressed / released + sticks, after the dead zone
  // F6.09 (B2) pointer, Unity names. Position in the renderer's virtual pixels, origin top-left (the Camera API's screen space).
  // Button 0 is the left mouse button or a finger; other buttons are not reported. Without a pointer they read false / (0, 0).
  static bool mousePresent();
  static Vector2 mousePosition();
  static bool GetMouseButton(i32 button);
  static bool GetMouseButtonDown(i32 button);  // went down this frame
  static bool GetMouseButtonUp(i32 button);

  // ---- Actions API ----
  // A pointer into the fixed action table: valid until the next loadActions / clearActions (re-Find in Start).
  static InputAction* Find(const char* name);
  static u32 actionCount();
  static InputAction* actionAt(u32 index);

  // ---- Rumble ----
  // low = heavy motor, high = light motor, both 0..1, for `seconds`. Returns false (and does nothing) when the target
  // has no rumble. SetVibration(0, 0, 0) stops it.
  static bool SetVibration(Scalar low, Scalar high, Scalar seconds);
  static void StopVibration() { SetVibration(0, 0, 0); }
  static bool rumbleSupported();
  static void setRumbleSupported(bool supported);
  static void currentVibration(Scalar& low, Scalar& high, Scalar& secondsLeft);
  static void bindPlatform(IPlatform* platform);  // where flushVibration sends changes
  static void flushVibration();                   // GameLoop calls it at the end of every frame

  // ---- shared helpers (backends and tests) ----
  // Radial dead zone: 0 below `dead`, then a linear ramp to full length at `outer`; direction is kept.
  static Vector2 ApplyRadialDeadZone(Vector2 v, Scalar dead, Scalar outer);
  // Raw analog byte (0..255, 128 = centre, as on PSP and DualShock) to -1..1; no dead zone.
  static Scalar stickFromByte(u8 value);
};

// Unity-style entry point of the Actions API: InputActions::Find("Jump").
class InputActions {
 public:
  static InputAction* Find(const char* name) { return Input::Find(name); }
};

}  // namespace rensga
