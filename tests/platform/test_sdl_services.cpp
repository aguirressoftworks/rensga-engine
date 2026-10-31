// F5.15 / F5.16 desktop seams of SdlPlatform: text input for TouchScreenKeyboard (typed text, Backspace, Return, Escape),
// the system language from the locale and the "no CPU clock" answer. Needs a window, so it runs under xvfb-run (name prefix sdl_services_).
#include <SDL.h>
#include <stdlib.h>
#include <string.h>

#include "rensga/core/system.h"
#include "rensga/platform/sdl.h"
#include "rensga_test.h"

using namespace rensga;

TEST(sdl_services_text_input_and_language) {
  setenv("LANG", "de_DE.UTF-8", 1);  // SDL lists LANG first, then LANGUAGE
  setenv("LC_ALL", "de_DE.UTF-8", 1);
  SdlPlatform p;
  if (!p.init()) { printf("skip: no SDL window\n"); return; }
  VirtualGamepad pad;
  CHECK(!p.setCpuClock(333));  // unsupported on the desktop
  CHECK_EQ(p.language(), 4u);  // SystemLanguage::German
  CHECK(p.batteryPercent() >= -1 && p.batteryPercent() <= 100);
  CHECK(p.systemMemoryBytes() > 0);

  system_services::bind(&p);
  CHECK(TouchScreenKeyboard::Open("Ab", "Name", 6));
  CHECK(!TouchScreenKeyboard::Open("x", "", 6));  // one keyboard at a time
  p.injectText("cd");
  p.injectText("efgh");  // the limit is 6 characters
  p.pollInput(pad);
  CHECK(TouchScreenKeyboard::status() == TouchScreenKeyboard::Status::Visible);
  p.injectKey(SDL_SCANCODE_BACKSPACE, true);
  p.injectKey(SDL_SCANCODE_BACKSPACE, false);
  p.pollInput(pad);
  p.injectKey(SDL_SCANCODE_RETURN, true);
  p.pollInput(pad);
  CHECK(TouchScreenKeyboard::status() == TouchScreenKeyboard::Status::Done);
  CHECK(strcmp(TouchScreenKeyboard::text(), "Abcde") == 0);  // "Abcdef" (6), one Backspace

  CHECK(TouchScreenKeyboard::Open("zz", "", 8));
  p.injectKey(SDL_SCANCODE_ESCAPE, true);
  p.pollInput(pad);
  CHECK(TouchScreenKeyboard::status() == TouchScreenKeyboard::Status::Canceled);
  CHECK(!p.shouldQuit());  // Escape closes the keyboard, it does not quit the player
  system_services::bind(nullptr);
  p.shutdown();
}
