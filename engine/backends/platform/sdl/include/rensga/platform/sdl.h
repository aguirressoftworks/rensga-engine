// SDL2 platform backend (desktop + Android): window, GL context, quit events, clock,
// keyboard + SDL_GameController + touch (TouchPad, F2.01) -> VirtualGamepad. The file system is in sdl_filesystem.h.
// Owns the window and GL context; render/gl draws into SDL_GL_GetCurrentWindow().
#pragma once
#include "rensga/core/backends.h"
#include "rensga/platform/touch.h"

namespace rensga {

class SdlPlatform : public IPlatform {
 public:
  bool init() override;
  void shutdown() override;
  void pollInput(VirtualGamepad& pad) override;  // keyboard + first game controller
  // The default bindings as pure functions, so another host (the editor's Game view) maps input exactly like the player.
  // `down[sc]` != 0: SDL scancode sc (< 512) is held. `controller` is an SDL_GameController* or null.
  static void applyKeyboard(const unsigned char* down, VirtualGamepad& pad);
  static void applyController(void* controller, VirtualGamepad& pad);
  double timeSeconds() override;
  bool shouldQuit() override { return quit_; }
  // Size of the renderer's virtual image: touch zones are laid out and hit-tested in it (default 480x272).
  void setVirtualResolution(u32 w, u32 h) { touchView_[0] = w; touchView_[1] = h; }
  // Touch overlay: shown on Android always, elsewhere once a finger has touched the screen.
  bool touchOverlayVisible() const;
  const TouchZones& touchZones() const { return touch_.zones(); }
  TouchPad& touch() { return touch_; }  // exposed for tests
  // F3.06: SDL_GameControllerRumble on the first controller; others ignore it. `seconds` becomes the SDL duration.
  void setVibration(Scalar low, Scalar high, Scalar seconds) override;
  // Dev and test hooks: a virtual SDL game controller (no hardware needed) and keyboard events injected as SDL would
  // deliver them. The controller is opened like a real one, so everything below pollInput is the production path.
  // Axis/button numbers are SDL_GameControllerAxis / SDL_GameControllerButton values. Rumble sent to the virtual
  // controller is stored in virtualRumble().
  bool attachVirtualPad();
  void setVirtualAxis(int axis, short value);
  void setVirtualButton(int button, bool down);
  void injectKey(int sdlScancode, bool down);
  void virtualRumble(unsigned short& low, unsigned short& high) const { low = vrLow_; high = vrHigh_; }
  // --inject-demo of rensga_desktop: attaches the virtual pad and drives sticks, triggers, buttons and keys by frame number.
  void injectDemo(unsigned frame);
  static void virtualRumbleSink(void* user, unsigned short low, unsigned short high);

 private:
  void openFirstController();
  void* pad_ = nullptr;  // SDL_GameController*
  void* window_ = nullptr;
  void* gl_ = nullptr;
  bool quit_ = false;
  TouchPad touch_;
  u32 touchView_[2] = {480, 272};
  unsigned char evKeys_[512] = {};  // keys held according to SDL_KEYDOWN / SDL_KEYUP events (covers injected events)
  int virtualId_ = -1;
  unsigned short vrLow_ = 0, vrHigh_ = 0;
};

}  // namespace rensga
