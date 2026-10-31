// PSP platform (PSP Engine 1, F4.03). Our own PSP_MODULE_INFO + callback thread stay in charge of exit and power; SDL2 is
// used only for the game controller (and, through SdlAudio, the audio device). SDL video is never initialised: the
// GU renderer owns the display. SDL_MAIN_HANDLED + no SDL2main, so SDL never installs its own callbacks.
#include <SDL.h>
#include <pspctrl.h>
#include <pspkernel.h>
#include <psppower.h>

#include "rensga/platform/psp.h"
#include "rensga/platform/psp_pad_map.h"

#ifndef RENSGA_PSP_CPU_MHZ
#define RENSGA_PSP_CPU_MHZ 333
#endif

namespace rensga {

// Flags only: the callback thread never touches engine state.
static volatile bool g_quit = false;
static volatile unsigned g_suspends = 0, g_resumes = 0;

static int exitCallback(int, int, void*) {
  g_quit = true;
  return 0;
}

static int powerCallback(int, int flags, void*) {
  if (flags & PSP_POWER_CB_SUSPENDING) ++g_suspends;
  if (flags & PSP_POWER_CB_RESUME_COMPLETE) ++g_resumes;
  return 0;
}

static int callbackThread(SceSize, void*) {
  int cb = sceKernelCreateCallback("rensga exit", exitCallback, nullptr);
  sceKernelRegisterExitCallback(cb);
  int pcb = sceKernelCreateCallback("rensga power", powerCallback, nullptr);
  scePowerRegisterCallback(0, pcb);
  sceKernelSleepThreadCB();
  return 0;
}

unsigned PspPlatform::suspendCount() { return g_suspends; }
unsigned PspPlatform::resumeCount() { return g_resumes; }

bool PspPlatform::init() {
  int th = sceKernelCreateThread("rensga cb", callbackThread, 0x11, 0xFA0, PSP_THREAD_ATTR_USER, nullptr);
  if (th >= 0) sceKernelStartThread(th, 0, nullptr);
  const int mhz = RENSGA_PSP_CPU_MHZ;
  scePowerSetClockFrequency(mhz, mhz, mhz / 2);
  RLOG_INFO("platform", "rensga: psp clock cpu=%d bus=%d (requested %d MHz)", scePowerGetCpuClockFrequencyInt(),
            scePowerGetBusClockFrequencyInt(), mhz);
  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

  const char* reason = nullptr;
#if defined(RENSGA_PSP_NATIVE_PAD)
  reason = "built with RENSGA_PSP_NATIVE_PAD";
#else
  SDL_SetMainReady();
  if (SDL_Init(SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
    RLOG_WARN("platform", "rensga: SDL_Init failed: %s", SDL_GetError());
    reason = "SDL_Init failed";
  } else {
    sdlReady_ = true;
    // The game polls the pad itself: no SDL event queue (its entries would be heap allocations every frame).
    SDL_GameControllerEventState(SDL_IGNORE);
    SDL_JoystickEventState(SDL_IGNORE);
    const int n = SDL_NumJoysticks();
    for (int i = 0; i < n && !sdlPad_; ++i)
      if (SDL_IsGameController(i)) sdlPad_ = SDL_GameControllerOpen(i);
    if (!sdlPad_) reason = n > 0 ? "no SDL game controller mapping for the pad" : "SDL reports no joystick";
  }
#endif
  if (sdlPad_) {
    RLOG_INFO("platform", "rensga: input sdl controller=%s", SDL_GameControllerName(static_cast<SDL_GameController*>(sdlPad_)));
  } else {
    RLOG_WARN("platform", "rensga: input native (sceCtrl): %s", reason ? reason : "unknown");
  }
  return true;
}

void PspPlatform::shutdown() {
  if (sdlPad_) SDL_GameControllerClose(static_cast<SDL_GameController*>(sdlPad_));
  sdlPad_ = nullptr;
  if (sdlReady_) SDL_Quit();
  sdlReady_ = false;
  sceKernelExitGame();
}

static_assert(psp_pad::kCross == PSP_CTRL_CROSS && psp_pad::kCircle == PSP_CTRL_CIRCLE && psp_pad::kSquare == PSP_CTRL_SQUARE &&
              psp_pad::kTriangle == PSP_CTRL_TRIANGLE && psp_pad::kLTrigger == PSP_CTRL_LTRIGGER &&
              psp_pad::kRTrigger == PSP_CTRL_RTRIGGER && psp_pad::kStart == PSP_CTRL_START && psp_pad::kSelect == PSP_CTRL_SELECT &&
              psp_pad::kUp == PSP_CTRL_UP && psp_pad::kDown == PSP_CTRL_DOWN && psp_pad::kLeft == PSP_CTRL_LEFT &&
              psp_pad::kRight == PSP_CTRL_RIGHT,
              "psp_pad_map.h bit values must match pspctrl.h");

// F4.03 check: the Home button has no game-pad mapping (the system takes it as the exit callback / Home popup), but sceCtrl still
// reports PSP_CTRL_HOME. Log the rising edge once per press so a test can see whether anything reaches the game.
static void noteHome(unsigned buttons) {
  static bool down = false;
  const bool now = (buttons & PSP_CTRL_HOME) != 0;
  if (now && !down) RLOG_INFO("platform", "rensga: input home bit seen");
  down = now;
}

void PspPlatform::pollInput(VirtualGamepad& pad) {
  if (sdlPad_) {
    SceCtrlData hd;
    if (sceCtrlPeekBufferPositive(&hd, 1) > 0) noteHome(hd.Buttons);
    SDL_GameController* c = static_cast<SDL_GameController*>(sdlPad_);
    SDL_GameControllerUpdate();
    auto b = [c](SDL_GameControllerButton id) { return SDL_GameControllerGetButton(c, id) != 0; };
    psp_pad::SdlPadState s;
    s.a = b(SDL_CONTROLLER_BUTTON_A);
    s.b = b(SDL_CONTROLLER_BUTTON_B);
    s.x = b(SDL_CONTROLLER_BUTTON_X);
    s.y = b(SDL_CONTROLLER_BUTTON_Y);
    s.leftShoulder = b(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    s.rightShoulder = b(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    s.start = b(SDL_CONTROLLER_BUTTON_START);
    s.back = b(SDL_CONTROLLER_BUTTON_BACK);
    s.dpUp = b(SDL_CONTROLLER_BUTTON_DPAD_UP);
    s.dpDown = b(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    s.dpLeft = b(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    s.dpRight = b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    s.leftX = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    s.leftY = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY);
    psp_pad::mapSdl(s, pad);
    if (osk_) pad = VirtualGamepad();
    return;
  }
  SceCtrlData d;
  sceCtrlPeekBufferPositive(&d, 1);
  noteHome(d.Buttons);
  psp_pad::map(d.Buttons, d.Lx, d.Ly, pad);
  if (osk_) pad = VirtualGamepad();  // the dialog owns the pad
}

double PspPlatform::timeSeconds() {
  return static_cast<double>(sceKernelGetSystemTimeWide()) * 1e-6;
}

bool PspPlatform::shouldQuit() { return g_quit; }

}  // namespace rensga
