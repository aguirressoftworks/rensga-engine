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

// F4.03: the same pad read through SDL's game controller layer. SDL's built-in "PSP builtin joypad" mapping is
// a:b2(cross) b:b1(circle) x:b3(square) y:b0(triangle) leftshoulder:b4 rightshoulder:b5 back:b10(select) start:b11
// dp*:b6..b9 leftx:a0 lefty:a1 (Y grows downward). The Home button has no mapping (the system takes it as the exit callback).
// This struct is the part of SDL_GameController the engine reads, so a host test can drive it without SDL.
struct SdlPadState {
  bool a = false, b = false, x = false, y = false;  // SDL_CONTROLLER_BUTTON_A/B/X/Y
  bool leftShoulder = false, rightShoulder = false, start = false, back = false;
  bool dpUp = false, dpDown = false, dpLeft = false, dpRight = false;
  i16 leftX = 0, leftY = 0;  // SDL_GameControllerGetAxis(LEFTX / LEFTY): -32768..32767, Y grows downward
};

// SDL axis back to the PSP's raw nub byte (128 = centre), so both paths share Input::stickFromByte and its scaling.
inline u8 byteFromSdlAxis(i16 v) {
  const int b = (static_cast<int>(v) >> 8) + 128;
  return static_cast<u8>(b < 0 ? 0 : (b > 255 ? 255 : b));
}

inline unsigned buttonsFromSdl(const SdlPadState& s) {
  unsigned b = 0;
  if (s.a) b |= kCross;
  if (s.b) b |= kCircle;
  if (s.x) b |= kSquare;
  if (s.y) b |= kTriangle;
  if (s.leftShoulder) b |= kLTrigger;
  if (s.rightShoulder) b |= kRTrigger;
  if (s.start) b |= kStart;
  if (s.back) b |= kSelect;
  if (s.dpUp) b |= kUp;
  if (s.dpDown) b |= kDown;
  if (s.dpLeft) b |= kLeft;
  if (s.dpRight) b |= kRight;
  return b;
}

// Same semantics as map(): this is map(buttonsFromSdl(s), byteFromSdlAxis(..)) so the two input paths cannot drift apart.
inline void mapSdl(const SdlPadState& s, VirtualGamepad& pad) {
  map(buttonsFromSdl(s), byteFromSdlAxis(s.leftX), byteFromSdlAxis(s.leftY), pad);
}

}  // namespace psp_pad
}  // namespace rensga
