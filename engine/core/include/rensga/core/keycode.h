// F3.06: Unity's KeyCode names and the key state a platform reports. Keys exist only where the platform has a keyboard
// (SDL desktop builds); everywhere else the state stays empty and Input::GetKey returns false.
#pragma once
#include "rensga/core/types.h"

namespace rensga {

enum class KeyCode : u8 {
  None = 0,
  Backspace, Tab, Return, Escape, Space,
  Alpha0, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  UpArrow, DownArrow, LeftArrow, RightArrow,
  LeftShift, RightShift, LeftControl, RightControl, LeftAlt, RightAlt,
  Home, End, PageUp, PageDown, Insert, Delete,
  Comma, Period, Slash, Minus, Equals, Semicolon, Quote, LeftBracket, RightBracket, Backslash, BackQuote,
  Count
};
static_assert(static_cast<unsigned>(KeyCode::Count) <= 128, "KeyState holds 128 keys");

// 128 keys as two 64-bit words: plain data, copied with the VirtualGamepad.
struct KeyState {
  u64 bits[2] = {0, 0};
  bool get(KeyCode k) const {
    const unsigned i = static_cast<unsigned>(k);
    return i < 128 && ((bits[i >> 6] >> (i & 63)) & 1u) != 0;
  }
  void set(KeyCode k, bool down) {
    const unsigned i = static_cast<unsigned>(k);
    if (i >= 128) return;
    const u64 m = u64(1) << (i & 63);
    if (down) bits[i >> 6] |= m; else bits[i >> 6] &= ~m;
  }
  bool any() const { return (bits[0] | bits[1]) != 0; }
};

}  // namespace rensga
