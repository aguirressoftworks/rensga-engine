// F3.06: the PSP pad as a pure function (no PSP SDK headers), so a host unit test can check the mapping. The bit values
// are the PSP_CTRL_* values from pspctrl.h; psp_platform.cpp static_asserts them against the SDK.
// The PSP has one analog nub (left stick), no triggers beyond L/R as buttons and no vibration motors.
#pragma once
#include "rensga/core/backends.h"
#include "rensga/core/input.h"

namespace rensga {
namespace psp_pad {

constexpr unsigned kSelect = 0x000001, kStart = 0x000008, kUp = 0x000010, kRight = 0x000020, kDown = 0x000040, kLeft = 0x000080;
constexpr unsigned kLTrigger = 0x000100, kRTrigger = 0x000200;
constexpr unsigned kTriangle = 0x001000, kCircle = 0x002000, kCross = 0x004000, kSquare = 0x008000;

// `buttons` is SceCtrlData::Buttons, `lx` / `ly` the raw nub bytes (128 = centre, Y grows downward). Sticks come out raw;
// Input applies the dead zone. The right stick, analog triggers and keys stay empty.
inline void map(unsigned buttons, u8 lx, u8 ly, VirtualGamepad& pad) {
  pad = VirtualGamepad();
  const unsigned b = buttons;
  pad.leftX = Input::stickFromByte(lx);
  pad.leftY = -Input::stickFromByte(ly);  // the pad's +Y is up
  VirtualGamepad::Buttons& h = pad.held;
  h.south = (b & kCross) != 0;
  h.east = (b & kCircle) != 0;
  h.west = (b & kSquare) != 0;
  h.north = (b & kTriangle) != 0;
  h.l = (b & kLTrigger) != 0;
  h.r = (b & kRTrigger) != 0;
  h.start = (b & kStart) != 0;
  h.select = (b & kSelect) != 0;
  h.up = (b & kUp) != 0;
  h.down = (b & kDown) != 0;
  h.left = (b & kLeft) != 0;
  h.right = (b & kRight) != 0;
}

}  // namespace psp_pad
}  // namespace rensga
