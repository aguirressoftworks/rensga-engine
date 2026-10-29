// SDL2 platform backend (desktop + Android): window, GL context, quit events, clock.
// Owns the window and GL context; render/gl draws into SDL_GL_GetCurrentWindow().
#pragma once
#include "rensga/core/backends.h"

namespace rensga {

class SdlPlatform : public IPlatform {
 public:
  bool init() override;
  void shutdown() override;
  void pollInput(VirtualGamepad& pad) override;  // gamepad/keyboard mapping lands with Engine 2
  double timeSeconds() override;
  bool shouldQuit() override { return quit_; }

 private:
  void* window_ = nullptr;
  void* gl_ = nullptr;
  bool quit_ = false;
};

}  // namespace rensga
