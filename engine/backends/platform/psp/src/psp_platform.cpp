#include <pspctrl.h>
#include <pspkernel.h>
#include <psppower.h>

#include "rensga/platform/psp.h"

namespace rensga {

static volatile bool g_quit = false;

static int exitCallback(int, int, void*) {
  g_quit = true;
  return 0;
}

static int callbackThread(SceSize, void*) {
  int cb = sceKernelCreateCallback("rensga exit", exitCallback, nullptr);
  sceKernelRegisterExitCallback(cb);
  sceKernelSleepThreadCB();
  return 0;
}

bool PspPlatform::init() {
  int th = sceKernelCreateThread("rensga cb", callbackThread, 0x11, 0xFA0, PSP_THREAD_ATTR_USER, nullptr);
  if (th >= 0) sceKernelStartThread(th, 0, nullptr);
  scePowerSetClockFrequency(333, 333, 166);
  sceCtrlSetSamplingCycle(0);
  sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
  return true;
}

void PspPlatform::shutdown() { sceKernelExitGame(); }

static Scalar nub(u8 v) {
  Scalar f = (static_cast<Scalar>(v) - 128.0f) / 127.0f;
  if (f > -0.15f && f < 0.15f) return 0;  // dead zone
  return f > 1 ? 1 : (f < -1 ? -1 : f);
}

void PspPlatform::pollInput(VirtualGamepad& pad) {
  SceCtrlData d;
  sceCtrlPeekBufferPositive(&d, 1);
  unsigned b = d.Buttons;
  pad.leftX = nub(d.Lx);
  pad.leftY = -nub(d.Ly);  // PSP nub Y grows downward; the pad's +Y is up
  VirtualGamepad::Buttons& h = pad.held;
  h.south = b & PSP_CTRL_CROSS;
  h.east = b & PSP_CTRL_CIRCLE;
  h.west = b & PSP_CTRL_SQUARE;
  h.north = b & PSP_CTRL_TRIANGLE;
  h.l = b & PSP_CTRL_LTRIGGER;
  h.r = b & PSP_CTRL_RTRIGGER;
  h.start = b & PSP_CTRL_START;
  h.select = b & PSP_CTRL_SELECT;
  h.up = b & PSP_CTRL_UP;
  h.down = b & PSP_CTRL_DOWN;
  h.left = b & PSP_CTRL_LEFT;
  h.right = b & PSP_CTRL_RIGHT;
}

double PspPlatform::timeSeconds() {
  return static_cast<double>(sceKernelGetSystemTimeWide()) * 1e-6;
}

bool PspPlatform::shouldQuit() { return g_quit; }

}  // namespace rensga
