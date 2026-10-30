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
  double timeSeconds() override;
  bool shouldQuit() override { return quit_; }
  // Size of the renderer's virtual image: touch zones are laid out and hit-tested in it (default 480x272).
  void setVirtualResolution(u32 w, u32 h) { touchView_[0] = w; touchView_[1] = h; }
  // Touch overlay: shown on Android always, elsewhere once a finger has touched the screen.
  bool touchOverlayVisible() const;
  const TouchZones& touchZones() const { return touch_.zones(); }
  TouchPad& touch() { return touch_; }  // exposed for tests

 private:
  void openFirstController();
  void* pad_ = nullptr;  // SDL_GameController*
  void* window_ = nullptr;
  void* gl_ = nullptr;
  bool quit_ = false;
  TouchPad touch_;
  u32 touchView_[2] = {480, 272};
};

}  // namespace rensga
