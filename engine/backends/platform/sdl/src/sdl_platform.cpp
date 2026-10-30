#include "rensga/platform/sdl.h"

#include <SDL.h>

#include "rensga/core/log.h"

namespace rensga {

bool SdlPlatform::init() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
    RLOG_ERROR("sdl", "SDL_Init failed: %s", SDL_GetError());
    return false;
  }
#if defined(__ANDROID__)
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);  // GLES 3.0
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);  // GL 3.3
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_Window* w = SDL_CreateWindow("Rensga", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 544,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!w) {
    RLOG_ERROR("sdl", "SDL_CreateWindow failed: %s", SDL_GetError());
    return false;
  }
  window_ = w;
  gl_ = SDL_GL_CreateContext(w);
  if (!gl_) {
    RLOG_ERROR("sdl", "SDL_GL_CreateContext failed: %s", SDL_GetError());
    return false;
  }
  SDL_GL_SetSwapInterval(1);
  openFirstController();
  RLOG_INFO("sdl", "window + GL context ready");
  return true;
}

void SdlPlatform::shutdown() {
  if (pad_) SDL_GameControllerClose(static_cast<SDL_GameController*>(pad_));
  pad_ = nullptr;
  if (gl_) SDL_GL_DeleteContext(static_cast<SDL_GLContext>(gl_));
  if (window_) SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
  gl_ = nullptr;
  window_ = nullptr;
  SDL_Quit();
}

void SdlPlatform::openFirstController() {
  if (pad_) return;
  for (int i = 0; i < SDL_NumJoysticks(); ++i) {
    if (!SDL_IsGameController(i)) continue;
    pad_ = SDL_GameControllerOpen(i);
    if (pad_) {
      RLOG_INFO("sdl", "controller: %s", SDL_GameControllerName(static_cast<SDL_GameController*>(pad_)));
      return;
    }
  }
}

// Stick value to -1..1 with a dead zone (same 0.15 as the PSP nub).
static Scalar axisValue(Sint16 v) {
  Scalar f = static_cast<Scalar>(v) / 32767.0f;
  if (f > -0.15f && f < 0.15f) return 0;
  return f > 1 ? 1 : (f < -1 ? -1 : f);
}

// Default bindings (match the PSP template): WASD = left stick, Space/Z = south, X = east, C = west,
// V = north, arrows = d-pad, Home/End = L/R (camera), Enter = start, Backspace = select, Esc = quit.
void SdlPlatform::pollInput(VirtualGamepad& pad) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) quit_ = true;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) quit_ = true;
    if (e.type == SDL_CONTROLLERDEVICEADDED) openFirstController();
    if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad_ &&
        e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(static_cast<SDL_GameController*>(pad_)))) {
      SDL_GameControllerClose(static_cast<SDL_GameController*>(pad_));
      pad_ = nullptr;
      openFirstController();
    }
  }
  pad = VirtualGamepad();
  const Uint8* k = SDL_GetKeyboardState(nullptr);
  VirtualGamepad::Buttons& h = pad.held;
  h.south = k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_Z];
  h.east = k[SDL_SCANCODE_X];
  h.west = k[SDL_SCANCODE_C];
  h.north = k[SDL_SCANCODE_V];
  h.up = k[SDL_SCANCODE_UP];
  h.down = k[SDL_SCANCODE_DOWN];
  h.left = k[SDL_SCANCODE_LEFT];
  h.right = k[SDL_SCANCODE_RIGHT];
  h.l = k[SDL_SCANCODE_HOME];
  h.r = k[SDL_SCANCODE_END];
  h.start = k[SDL_SCANCODE_RETURN];
  h.select = k[SDL_SCANCODE_BACKSPACE];
  Scalar x = static_cast<Scalar>(k[SDL_SCANCODE_D]) - static_cast<Scalar>(k[SDL_SCANCODE_A]);
  Scalar y = static_cast<Scalar>(k[SDL_SCANCODE_W]) - static_cast<Scalar>(k[SDL_SCANCODE_S]);
  if (x != 0 && y != 0) {  // diagonal keys: unit length, like a stick
    x *= 0.70710678f;
    y *= 0.70710678f;
  }
  pad.leftX = x;
  pad.leftY = y;

  if (pad_) {
    SDL_GameController* c = static_cast<SDL_GameController*>(pad_);
    auto b = [c](SDL_GameControllerButton id) { return SDL_GameControllerGetButton(c, id) != 0; };
    // South/east/west/north are physical positions (A/B/X/Y on an Xbox layout), not labels.
    h.south = h.south || b(SDL_CONTROLLER_BUTTON_A);
    h.east = h.east || b(SDL_CONTROLLER_BUTTON_B);
    h.west = h.west || b(SDL_CONTROLLER_BUTTON_X);
    h.north = h.north || b(SDL_CONTROLLER_BUTTON_Y);
    h.l = h.l || b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    h.r = h.r || b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    h.start = h.start || b(SDL_CONTROLLER_BUTTON_START);
    h.select = h.select || b(SDL_CONTROLLER_BUTTON_BACK);
    h.up = h.up || b(SDL_CONTROLLER_BUTTON_DPAD_UP);
    h.down = h.down || b(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    h.left = h.left || b(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    h.right = h.right || b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    Scalar sx = axisValue(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX));
    Scalar sy = -axisValue(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));  // SDL Y grows downward
    if (sx != 0 || sy != 0) {
      pad.leftX = sx;
      pad.leftY = sy;
    }
  }
}

double SdlPlatform::timeSeconds() { return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency()); }

}  // namespace rensga
