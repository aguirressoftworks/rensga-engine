// F4.03: the PSP pad read through SDL's game controller layer (psp_pad::mapSdl) must mean exactly what the native sceCtrl path
// (psp_pad::map) means. The SDL side is built here from SDL's own "PSP builtin joypad" mapping and its PSP joystick driver:
//   a:b2(cross) b:b1(circle) x:b3(square) y:b0(triangle) leftshoulder:b4 rightshoulder:b5 back:b10(select) start:b11
//   dpup:b8 dpdown:b6 dpleft:b7 dpright:b9, axes (raw byte - 128) * 256, SDL Y grows downward (like the nub byte).
// No SDL calls: the on-device half (the real SDL_GameController on PSP) is checked by the PPSSPP boot log "rensga: input sdl controller=".
#include "rensga/core/input.h"
#include "rensga/platform/psp_pad_map.h"
#include "rensga_test.h"

using namespace rensga;

namespace {

psp_pad::SdlPadState sdlStateFromPsp(unsigned buttons, u8 lx, u8 ly) {
  psp_pad::SdlPadState s;
  s.a = (buttons & psp_pad::kCross) != 0;
  s.b = (buttons & psp_pad::kCircle) != 0;
  s.x = (buttons & psp_pad::kSquare) != 0;
  s.y = (buttons & psp_pad::kTriangle) != 0;
  s.leftShoulder = (buttons & psp_pad::kLTrigger) != 0;
  s.rightShoulder = (buttons & psp_pad::kRTrigger) != 0;
  s.start = (buttons & psp_pad::kStart) != 0;
  s.back = (buttons & psp_pad::kSelect) != 0;
  s.dpUp = (buttons & psp_pad::kUp) != 0;
  s.dpDown = (buttons & psp_pad::kDown) != 0;
  s.dpLeft = (buttons & psp_pad::kLeft) != 0;
  s.dpRight = (buttons & psp_pad::kRight) != 0;
  s.leftX = static_cast<i16>((static_cast<int>(lx) - 128) * 256);
  s.leftY = static_cast<i16>((static_cast<int>(ly) - 128) * 256);
  return s;
}

bool same(const VirtualGamepad& a, const VirtualGamepad& b) {
  const VirtualGamepad::Buttons &x = a.held, &y = b.held;
  return x.south == y.south && x.east == y.east && x.west == y.west && x.north == y.north && x.l == y.l && x.r == y.r &&
         x.start == y.start && x.select == y.select && x.up == y.up && x.down == y.down && x.left == y.left && x.right == y.right &&
         x.l2 == y.l2 && x.r2 == y.r2 && a.leftX == b.leftX && a.leftY == b.leftY && a.rightX == b.rightX && a.rightY == b.rightY;
}

}  // namespace

TEST(pad_psp_sdl_matches_native_every_button_combination) {
  static const unsigned bits[12] = {psp_pad::kSelect, psp_pad::kStart,  psp_pad::kUp,       psp_pad::kRight,
                                    psp_pad::kDown,   psp_pad::kLeft,   psp_pad::kLTrigger, psp_pad::kRTrigger,
                                    psp_pad::kTriangle, psp_pad::kCircle, psp_pad::kCross,  psp_pad::kSquare};
  static const u8 nub[][2] = {{128, 128}, {0, 0}, {255, 255}, {0, 255}, {255, 0}, {134, 122}, {64, 192}, {127, 129}};
  u32 checked = 0;
  for (unsigned mask = 0; mask < (1u << 12); ++mask) {
    unsigned buttons = 0;
    for (int i = 0; i < 12; ++i)
      if (mask & (1u << i)) buttons |= bits[i];
    for (const auto& n : nub) {
      VirtualGamepad native, viaSdl;
      psp_pad::map(buttons, n[0], n[1], native);
      psp_pad::mapSdl(sdlStateFromPsp(buttons, n[0], n[1]), viaSdl);
      if (!same(native, viaSdl)) {
        fprintf(stderr, "  mismatch buttons=%06x nub=%u,%u\n", buttons, n[0], n[1]);
        CHECK(false);
        return;
      }
      ++checked;
    }
  }
  CHECK(checked == 4096u * 8u);
}

TEST(pad_psp_sdl_axis_byte_roundtrip) {
  for (int b = 0; b < 256; ++b) CHECK(psp_pad::byteFromSdlAxis(static_cast<i16>((b - 128) * 256)) == b);
  CHECK(psp_pad::byteFromSdlAxis(-32768) == 0);
  CHECK(psp_pad::byteFromSdlAxis(32767) == 255);
  CHECK(psp_pad::byteFromSdlAxis(0) == 128);
}

TEST(pad_psp_sdl_buttons_individually) {
  psp_pad::SdlPadState s;
  VirtualGamepad pad;
  s.a = true;
  psp_pad::mapSdl(s, pad);
  CHECK(pad.held.south && !pad.held.east && !pad.held.west && !pad.held.north);
  s = psp_pad::SdlPadState();
  s.y = true; s.dpLeft = true; s.back = true;
  psp_pad::mapSdl(s, pad);
  CHECK(pad.held.north && pad.held.left && pad.held.select && !pad.held.south && !pad.held.start);
  s = psp_pad::SdlPadState();
  s.leftY = -32768;  // nub pushed up: SDL Y negative -> pad +Y
  psp_pad::mapSdl(s, pad);
  CHECK_NEAR(pad.leftY, 1.0f, 1e-6f);
}
