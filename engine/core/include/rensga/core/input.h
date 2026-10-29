// MVP-18/29: input actions by name over the VirtualGamepad. Actions come from the pack INPT section
// (cooked from ProjectSettings/InputActions.json) or are defined in code.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/pack_format.h"

namespace rensga {

class Input {
 public:
  static constexpr u32 kMaxActions = 16;
  static constexpr u32 kMaxNameLength = 24;

  // Clears all actions and loads the pack's INPT table (strings resolved through `pack`).
  static void loadActions(const PackView& pack, const PackInputAction* actions, u32 count);
  static void defineButton(const char* name, PackInputSource source);
  static void defineAxis2(const char* name, PackInputSource source);
  static void clearActions();

  // Called once per frame by GameLoop with the freshly polled pad.
  static void update(const VirtualGamepad& pad);

  // First axis of the action (x of an Axis2). 0 if the action is unknown.
  static Scalar GetAxis(const char* name);
  static Vector2 GetAxis2(const char* name);  // Move -> (x, y), y up
  static bool GetButton(const char* name);
  static bool GetButtonDown(const char* name);  // went down this frame
  static const VirtualGamepad& pad();
};

}  // namespace rensga
