#include "rensga/platform/sdl.h"

#include <SDL.h>
#include <string.h>

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

// Raw stick value to -1..1. The dead zone and outer clamp are applied by Input (InputActions.json).
static Scalar axisValue(Sint16 v) {
  Scalar f = static_cast<Scalar>(v) / 32767.0f;
  return f > 1 ? 1 : (f < -1 ? -1 : f);
}

// SDL scancode -> Unity KeyCode (KeyCode::None for keys we don't name).
static KeyCode keyFromScancode(int sc) {
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return static_cast<KeyCode>(static_cast<int>(KeyCode::A) + (sc - SDL_SCANCODE_A));
  if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return static_cast<KeyCode>(static_cast<int>(KeyCode::Alpha1) + (sc - SDL_SCANCODE_1));
  if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + (sc - SDL_SCANCODE_F1));
  switch (sc) {
    case SDL_SCANCODE_0: return KeyCode::Alpha0;
    case SDL_SCANCODE_BACKSPACE: return KeyCode::Backspace;
    case SDL_SCANCODE_TAB: return KeyCode::Tab;
    case SDL_SCANCODE_RETURN: return KeyCode::Return;
    case SDL_SCANCODE_ESCAPE: return KeyCode::Escape;
    case SDL_SCANCODE_SPACE: return KeyCode::Space;
    case SDL_SCANCODE_UP: return KeyCode::UpArrow;
    case SDL_SCANCODE_DOWN: return KeyCode::DownArrow;
    case SDL_SCANCODE_LEFT: return KeyCode::LeftArrow;
    case SDL_SCANCODE_RIGHT: return KeyCode::RightArrow;
    case SDL_SCANCODE_LSHIFT: return KeyCode::LeftShift;
    case SDL_SCANCODE_RSHIFT: return KeyCode::RightShift;
    case SDL_SCANCODE_LCTRL: return KeyCode::LeftControl;
    case SDL_SCANCODE_RCTRL: return KeyCode::RightControl;
    case SDL_SCANCODE_LALT: return KeyCode::LeftAlt;
    case SDL_SCANCODE_RALT: return KeyCode::RightAlt;
    case SDL_SCANCODE_HOME: return KeyCode::Home;
    case SDL_SCANCODE_END: return KeyCode::End;
    case SDL_SCANCODE_PAGEUP: return KeyCode::PageUp;
    case SDL_SCANCODE_PAGEDOWN: return KeyCode::PageDown;
    case SDL_SCANCODE_INSERT: return KeyCode::Insert;
    case SDL_SCANCODE_DELETE: return KeyCode::Delete;
    case SDL_SCANCODE_COMMA: return KeyCode::Comma;
    case SDL_SCANCODE_PERIOD: return KeyCode::Period;
    case SDL_SCANCODE_SLASH: return KeyCode::Slash;
    case SDL_SCANCODE_MINUS: return KeyCode::Minus;
    case SDL_SCANCODE_EQUALS: return KeyCode::Equals;
    case SDL_SCANCODE_SEMICOLON: return KeyCode::Semicolon;
    case SDL_SCANCODE_APOSTROPHE: return KeyCode::Quote;
    case SDL_SCANCODE_LEFTBRACKET: return KeyCode::LeftBracket;
    case SDL_SCANCODE_RIGHTBRACKET: return KeyCode::RightBracket;
    case SDL_SCANCODE_BACKSLASH: return KeyCode::Backslash;
    case SDL_SCANCODE_GRAVE: return KeyCode::BackQuote;
    default: return KeyCode::None;
  }
}

// Default bindings (match the PSP template): WASD = left stick, Space/Z = south, X = east, C = west,
// V = north, arrows = d-pad, Home/End = L/R (camera), Enter = start, Backspace = select, Esc = quit.
// Controller: sticks, analog triggers (L2/R2 held past half), A/B/X/Y by position, shoulders, d-pad. GetKey sees every key.
void SdlPlatform::pollInput(VirtualGamepad& pad) {
  int dw = 0, dh = 0;
  if (window_) SDL_GL_GetDrawableSize(static_cast<SDL_Window*>(window_), &dw, &dh);
  touch_.setView(touchView_[0], touchView_[1], static_cast<u32>(dw), static_cast<u32>(dh));
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_FINGERDOWN) touch_.fingerDown(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
    if (e.type == SDL_FINGERMOTION) touch_.fingerMove(e.tfinger.fingerId, e.tfinger.x, e.tfinger.y);
    if (e.type == SDL_FINGERUP) touch_.fingerUp(e.tfinger.fingerId);
    if (e.type == SDL_QUIT) quit_ = true;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) quit_ = true;
    if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && e.key.keysym.scancode < 512)
      evKeys_[e.key.keysym.scancode] = e.type == SDL_KEYDOWN ? 1 : 0;
    if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) memset(evKeys_, 0, sizeof evKeys_);
    if (e.type == SDL_CONTROLLERDEVICEADDED) openFirstController();
    if (e.type == SDL_CONTROLLERDEVICEREMOVED && pad_ &&
        e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(static_cast<SDL_GameController*>(pad_)))) {
      SDL_GameControllerClose(static_cast<SDL_GameController*>(pad_));
      pad_ = nullptr;
      openFirstController();
    }
  }
  pad = VirtualGamepad();
  const Uint8* ks = SDL_GetKeyboardState(nullptr);
  // A key is down if SDL's keyboard state or the event stream says so (the stream also carries injected events).
  auto key = [ks, this](int sc) { return ks[sc] != 0 || evKeys_[sc] != 0; };
  for (int sc = 0; sc < 512; ++sc) {
    if (!key(sc)) continue;
    const KeyCode kc = keyFromScancode(sc);
    if (kc != KeyCode::None) pad.keys.set(kc, true);
  }
  VirtualGamepad::Buttons& h = pad.held;
  h.south = key(SDL_SCANCODE_SPACE) || key(SDL_SCANCODE_Z);
  h.east = key(SDL_SCANCODE_X);
  h.west = key(SDL_SCANCODE_C);
  h.north = key(SDL_SCANCODE_V);
  h.up = key(SDL_SCANCODE_UP);
  h.down = key(SDL_SCANCODE_DOWN);
  h.left = key(SDL_SCANCODE_LEFT);
  h.right = key(SDL_SCANCODE_RIGHT);
  h.l = key(SDL_SCANCODE_HOME);
  h.r = key(SDL_SCANCODE_END);
  h.start = key(SDL_SCANCODE_RETURN);
  h.select = key(SDL_SCANCODE_BACKSPACE);
  Scalar x = static_cast<Scalar>(key(SDL_SCANCODE_D)) - static_cast<Scalar>(key(SDL_SCANCODE_A));
  Scalar y = static_cast<Scalar>(key(SDL_SCANCODE_W)) - static_cast<Scalar>(key(SDL_SCANCODE_S));
  if (x != 0 && y != 0) {  // diagonal keys: unit length, like a stick
    x *= 0.70710678f;
    y *= 0.70710678f;
  }
  pad.leftX = x;
  pad.leftY = y;
  touch_.apply(pad);  // on-screen controls act like the keyboard / pad: same actions

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
    const Scalar sx = axisValue(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX));
    const Scalar sy = -axisValue(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));  // SDL Y grows downward
    if (sx * sx + sy * sy > 0.01f) {  // a resting stick doesn't override the keyboard; Input applies the real dead zone
      pad.leftX = sx;
      pad.leftY = sy;
    }
    pad.rightX = axisValue(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX));
    pad.rightY = -axisValue(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
    pad.l2Value = axisValue(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
    pad.r2Value = axisValue(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
    if (pad.l2Value < 0) pad.l2Value = 0;
    if (pad.r2Value < 0) pad.r2Value = 0;
    h.l2 = pad.l2Value >= 0.5f;
    h.r2 = pad.r2Value >= 0.5f;
  }
}

void SdlPlatform::setVibration(Scalar low, Scalar high, Scalar seconds) {
  if (!pad_) return;
  auto u16 = [](Scalar v) { return static_cast<Uint16>(v <= 0 ? 0 : (v >= 1 ? 65535 : v * 65535.0f)); };
  const Uint32 ms = seconds <= 0 ? 0 : static_cast<Uint32>(seconds * 1000.0f);
  SDL_GameControllerRumble(static_cast<SDL_GameController*>(pad_), u16(low), u16(high), ms);
}

void SdlPlatform::virtualRumbleSink(void* user, unsigned short low, unsigned short high) {
  SdlPlatform* p = static_cast<SdlPlatform*>(user);
  p->vrLow_ = low;
  p->vrHigh_ = high;
}

bool SdlPlatform::attachVirtualPad() {
  if (virtualId_ >= 0) return true;
  SDL_VirtualJoystickDesc d;
  SDL_zero(d);
  d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
  d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
  d.naxes = SDL_CONTROLLER_AXIS_MAX;
  d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
  d.userdata = this;
  d.Rumble = [](void* user, Uint16 low, Uint16 high) -> int {
    SdlPlatform::virtualRumbleSink(user, low, high);
    return 0;
  };
  const int id = SDL_JoystickAttachVirtualEx(&d);
  if (id < 0) {
    RLOG_ERROR("sdl", "virtual pad: %s", SDL_GetError());
    return false;
  }
  virtualId_ = id;
  if (pad_) {  // drop a real controller so the virtual one is the "first"
    SDL_GameControllerClose(static_cast<SDL_GameController*>(pad_));
    pad_ = nullptr;
  }
  pad_ = SDL_GameControllerOpen(id);
  if (!pad_) RLOG_ERROR("sdl", "virtual pad not usable as a game controller: %s", SDL_GetError());
  return pad_ != nullptr;
}

void SdlPlatform::injectDemo(unsigned frame) {
  if (frame == 0 && !attachVirtualPad()) return;
  if (frame < 6) return;
  setVirtualAxis(SDL_CONTROLLER_AXIS_LEFTX, 26000);        // left stick right/up, past the dead zone
  setVirtualAxis(SDL_CONTROLLER_AXIS_LEFTY, -16000);
  setVirtualAxis(SDL_CONTROLLER_AXIS_RIGHTX, -19000);      // right stick left/up
  setVirtualAxis(SDL_CONTROLLER_AXIS_RIGHTY, -12000);
  setVirtualAxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 22000);  // L2 about 0.67
  setVirtualAxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 9000);  // R2 about 0.27
  setVirtualButton(SDL_CONTROLLER_BUTTON_A, true);         // south
  setVirtualButton(SDL_CONTROLLER_BUTTON_Y, true);         // north
  setVirtualButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT, true);
  if (frame == 6) injectKey(SDL_SCANCODE_E, true);         // keyboard axis: ramps up over the next frames
  if (frame == 6) injectKey(SDL_SCANCODE_SPACE, true);
  if (frame == 20) {                                       // shoulder combo: rumble in the scene
    setVirtualButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, true);
    setVirtualButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, true);
  }
}

void SdlPlatform::setVirtualAxis(int axis, short value) {
  if (pad_) SDL_JoystickSetVirtualAxis(SDL_GameControllerGetJoystick(static_cast<SDL_GameController*>(pad_)), axis, value);
}
void SdlPlatform::setVirtualButton(int button, bool down) {
  if (pad_) SDL_JoystickSetVirtualButton(SDL_GameControllerGetJoystick(static_cast<SDL_GameController*>(pad_)), button, down ? 1 : 0);
}
void SdlPlatform::injectKey(int sdlScancode, bool down) {
  SDL_Event e;
  SDL_zero(e);
  e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
  e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
  e.key.keysym.scancode = static_cast<SDL_Scancode>(sdlScancode);
  e.key.keysym.sym = SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(sdlScancode));
  SDL_PushEvent(&e);
}

bool SdlPlatform::touchOverlayVisible() const {
#if defined(__ANDROID__)
  return true;
#else
  return touch_.used();
#endif
}

double SdlPlatform::timeSeconds() { return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency()); }

}  // namespace rensga
