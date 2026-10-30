#include <pspctrl.h>
#include <pspkernel.h>
#include <psppower.h>

#include "rensga/platform/psp.h"
#include "rensga/platform/psp_pad_map.h"

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

static_assert(psp_pad::kCross == PSP_CTRL_CROSS && psp_pad::kCircle == PSP_CTRL_CIRCLE && psp_pad::kSquare == PSP_CTRL_SQUARE &&
              psp_pad::kTriangle == PSP_CTRL_TRIANGLE && psp_pad::kLTrigger == PSP_CTRL_LTRIGGER &&
              psp_pad::kRTrigger == PSP_CTRL_RTRIGGER && psp_pad::kStart == PSP_CTRL_START && psp_pad::kSelect == PSP_CTRL_SELECT &&
              psp_pad::kUp == PSP_CTRL_UP && psp_pad::kDown == PSP_CTRL_DOWN && psp_pad::kLeft == PSP_CTRL_LEFT &&
              psp_pad::kRight == PSP_CTRL_RIGHT,
              "psp_pad_map.h bit values must match pspctrl.h");

void PspPlatform::pollInput(VirtualGamepad& pad) {
  SceCtrlData d;
  sceCtrlPeekBufferPositive(&d, 1);
  psp_pad::map(d.Buttons, d.Lx, d.Ly, pad);
}

double PspPlatform::timeSeconds() {
  return static_cast<double>(sceKernelGetSystemTimeWide()) * 1e-6;
}

bool PspPlatform::shouldQuit() { return g_quit; }

}  // namespace rensga
