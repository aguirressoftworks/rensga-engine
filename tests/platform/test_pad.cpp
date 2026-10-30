// F3.06: the PSP pad mapping as a pure function, and an SDL virtual controller + injected keyboard events through
// SdlPlatform -> Input (the production path below pollInput).
#include <SDL.h>

#include "rensga/core/input.h"
#include "rensga/platform/psp_pad_map.h"
#include "rensga/platform/sdl.h"
#include "rensga_test.h"

using namespace rensga;

TEST(pad_psp_map_buttons_and_nub) {
  VirtualGamepad pad;
  psp_pad::map(psp_pad::kCross | psp_pad::kLTrigger | psp_pad::kUp | psp_pad::kStart, 255, 0, pad);
  CHECK(pad.held.south && pad.held.l && pad.held.up && pad.held.start);
  CHECK(!pad.held.east && !pad.held.west && !pad.held.north && !pad.held.r && !pad.held.select);
  CHECK_NEAR(pad.leftX, 1.0f, 1e-6f);
  CHECK_NEAR(pad.leftY, 1.0f, 1e-6f);  // byte 0 = nub pushed up = +Y
  CHECK(pad.rightX == 0 && pad.rightY == 0 && !pad.keys.any());
  psp_pad::map(psp_pad::kCircle | psp_pad::kSquare | psp_pad::kTriangle | psp_pad::kSelect | psp_pad::kRTrigger |
                   psp_pad::kDown | psp_pad::kLeft | psp_pad::kRight, 128, 128, pad);
  CHECK(pad.held.east && pad.held.west && pad.held.north && pad.held.select && pad.held.r);
  CHECK(pad.held.down && pad.held.left && pad.held.right && !pad.held.south);
  CHECK(pad.leftX == 0 && pad.leftY == 0);
  // The nub's idle jitter (a few counts around 128) is removed by the dead zone.
  psp_pad::map(0, 134, 122, pad);
  Input::clearActions();
  Input::setStickSettings(0.2f, 0.95f);
  Input::defineAxis2("Move", PackInputSource::Nub);
  Input::update(pad);
  CHECK(Input::GetAxis2("Move").x == 0 && Input::GetAxis2("Move").y == 0);
  Input::clearActions();
}

TEST(pad_sdl_virtual_controller_and_keys) {
  static SdlPlatform plat;
  if (!plat.init()) {
    fprintf(stderr, "  CHECK failed: SdlPlatform::init (no display?)\n");
    ++rtest::failures();
    return;
  }
  CHECK(plat.attachVirtualPad());
  Input::clearActions();
  Input::setStickSettings(0.2f, 0.95f);
  Input::defineAxis2("Move", PackInputSource::Nub);
  Input::defineAxis2("Aim", PackInputSource::RightStick);
  Input::defineAxis("TriggerL", PackInputSource::AxisL2);
  Input::defineButton("Jump", PackInputSource::South);
  plat.setVirtualAxis(SDL_CONTROLLER_AXIS_LEFTX, 32767);
  plat.setVirtualAxis(SDL_CONTROLLER_AXIS_LEFTY, -32768);
  plat.setVirtualAxis(SDL_CONTROLLER_AXIS_RIGHTX, 3000);  // inside the dead zone
  plat.setVirtualAxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 16384);
  plat.setVirtualButton(SDL_CONTROLLER_BUTTON_A, true);
  plat.injectKey(SDL_SCANCODE_SPACE, true);
  VirtualGamepad pad;
  plat.pollInput(pad);
  plat.pollInput(pad);
  Input::update(pad);
  CHECK(Input::GetButtonDown("Jump"));
  CHECK(Input::GetAxis2("Move").x > 0.65f && Input::GetAxis2("Move").y > 0.65f);
  CHECK(Input::GetAxis2("Aim").x == 0);
  CHECK_NEAR(Input::GetAxis("TriggerL"), 0.75f, 0.02f);  // SDL maps a virtual trigger axis -32768..32767 to 0..1
  CHECK(Input::GetKey(KeyCode::Space) && Input::anyKey());
  // Rumble goes to the controller (the virtual one records it).
  Input::bindPlatform(&plat);
  Input::setRumbleSupported(true);
  CHECK(Input::SetVibration(0.5f, 1.0f, 0.5f));
  Input::flushVibration();
  unsigned short lo = 0, hi = 0;
  plat.virtualRumble(lo, hi);
  CHECK(lo > 30000 && lo < 36000 && hi >= 65000);
  Input::setRumbleSupported(false);
  Input::bindPlatform(nullptr);
  Input::clearActions();
  plat.shutdown();
}
