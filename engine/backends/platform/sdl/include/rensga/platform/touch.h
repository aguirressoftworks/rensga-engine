// F2.01: on-screen touch controls (Android, touch screens). Multi-touch with a fixed finger table (no heap).
// Touch positions arrive normalized to the window (SDL finger events); zones live in the VIRTUAL resolution of the
// renderer, which the GL renderer letterboxes into the window, so touches map through the same letterbox.
//   D-pad zone bottom-left (left/right/up/down), South and East bottom-right, Start top-centre.
// Pure logic (no SDL types): unit-tested without a window.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/math.h"

namespace rensga {

struct TouchZones {
  Rect dpad, south, east, start;  // virtual-resolution rectangles, for the overlay and for hit testing
};

class TouchPad {
 public:
  static constexpr u32 kMaxFingers = 10;

  // Virtual image size and the window (drawable) size it is letterboxed into.
  void setView(u32 virtualW, u32 virtualH, u32 windowW, u32 windowH);
  // Normalized window coordinates (0..1), as SDL reports them. `id` is SDL_FingerID.
  void fingerDown(i64 id, float nx, float ny);
  void fingerMove(i64 id, float nx, float ny);
  void fingerUp(i64 id);
  // Ors the buttons held by the current fingers into `pad`. A finger that went down and up between two calls still
  // counts once, so a quick tap is never lost. Call once per input poll.
  void apply(VirtualGamepad& pad);
  bool used() const { return used_; }  // a finger has touched the screen at least once
  const TouchZones& zones() const { return zones_; }

 private:
  struct Finger {
    i64 id = 0;
    float x = 0, y = 0;  // virtual coordinates
    bool active = false, released = false;
  };
  void toVirtual(float nx, float ny, float* vx, float* vy) const;
  Finger* find(i64 id);
  void layout();
  Finger fingers_[kMaxFingers];
  TouchZones zones_;
  u32 vw_ = 0, vh_ = 0, ww_ = 0, wh_ = 0;
  bool used_ = false;
};

}  // namespace rensga
