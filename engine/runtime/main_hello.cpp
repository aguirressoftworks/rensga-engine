// Engine 0 hello: the same main on every F1 target. Clears to the brand `bg` (dark) color and logs boot +
// frame heartbeats through the engine logger, using only IPlatform/IRenderer.
//   rensga_hello [--frames N]     (N = 0 or absent: run until the window closes / forever on PS2)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rensga/core/backends.h"
#include "rensga/core/log.h"
#include "rensga/core/memory.h"
#if defined(RENSGA_PLATFORM_PS2)
#include "rensga/platform/ps2.h"
#include "rensga/render/gs.h"
using PlatformImpl = rensga::Ps2Platform;
using FsImpl = rensga::Ps2FileSystem;
using RendererImpl = rensga::GsRenderer;
#else
#if defined(_WIN32)
#define SDL_MAIN_HANDLED  // plain console main(); no SDL2main/WinMain needed
#endif
#include <SDL.h>  // on Android this renames main to SDL_main (SDLActivity calls it)
#if defined(__ANDROID__)
#include <android/log.h>
#endif
#include "rensga/platform/sdl.h"
#include "rensga/platform/sdl_filesystem.h"
#include "rensga/render/gl.h"
using PlatformImpl = rensga::SdlPlatform;
using FsImpl = rensga::SdlFileSystem;
using RendererImpl = rensga::GlRenderer;
#endif

using namespace rensga;

#if defined(__ANDROID__)
// logcat sink (stdout is not visible on Android): `adb logcat -s rensga`.
class LogcatSink : public ILogSink {
 public:
  void write(const LogRecord& r) override {
    __android_log_print(r.level == LogLevel::Error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "rensga", "[%s] %s", r.channel, r.message);
  }
};
#endif

// Brand token `bg` (dark theme, soil.900 #1c1a16), see docs/rensga-engine/brand/visual-identity.md.
static const Color32 kBrandBg(0x1c, 0x1a, 0x16);

int main(int argc, char** argv) {
  u32 maxFrames = 0;
  for (int i = 1; i + 1 < argc; ++i)
    if (!strcmp(argv[i], "--frames")) maxFrames = static_cast<u32>(atoi(argv[i + 1]));

#if defined(__ANDROID__)
  static LogcatSink logcat;
  log::addSink(&logcat);
#endif
  RLOG_INFO("hello", "rensga hello: boot");
  static PlatformImpl platform;
  static RendererImpl renderer;
  if (!platform.init()) return 2;
  if (!renderer.init(960, 544)) return 3;
#if defined(RENSGA_PLATFORM_PS2)
  RLOG_INFO("hello", "rensga: ps2 video=%s region=%c", platform.info().pal ? "PAL" : "NTSC", platform.info().region);
#endif
  static FsImpl fs;
  {  // save:/ round trip: proves the virtual-path mapping on this platform (a read-only disc may fail; that is logged, not fatal)
    const char msg[] = "rensga hello";
    bool ok = fs.writeFile("save:/hello.txt", msg, sizeof(msg));
    RLOG_INFO("hello", "rensga: save:/hello.txt write %s", ok ? "ok" : "failed");
  }

  u32 frame = 0;
  while (!platform.shouldQuit() && (maxFrames == 0 || frame < maxFrames)) {
    VirtualGamepad pad;
    platform.pollInput(pad);
    renderer.beginFrame(kBrandBg);
    renderer.endFrame();
    ++frame;
    heap::noteFrameDone();  // contract 7: allocations after the first frame are counted
    if (frame == 1) heap::checkReset();  // the first frame includes lazy driver setup
    if (frame % 60 == 0) {
      RLOG_INFO("hello", "rensga: frame %u t=%.2f", static_cast<unsigned>(frame), platform.timeSeconds());
      fflush(stdout);
    }
    if (frame == 300 && maxFrames == 0) heap::logAllocCheck();  // endless runs (PS2, Android) log once
  }
  if (maxFrames != 0) heap::logAllocCheck();
  RLOG_INFO("hello", "rensga hello: done frames=%u", static_cast<unsigned>(frame));
#if !defined(RENSGA_PLATFORM_PS2)
  renderer.shutdown();  // GL objects go before the context
#endif
  platform.shutdown();
  return 0;
}
