#include "rensga/core/application.h"

#include "rensga/core/system.h"

namespace rensga {

namespace {
constexpr RuntimePlatform compiledPlatform() {
#if defined(__ANDROID__)
  return RuntimePlatform::Android;
#elif defined(__PSP__)
  return RuntimePlatform::Psp;
#elif defined(_EE) || defined(__PS2__) || defined(PS2)
  return RuntimePlatform::Ps2;
#elif defined(_WIN32)
  return RuntimePlatform::WindowsPlayer;
#else
  return RuntimePlatform::LinuxPlayer;
#endif
}
bool g_quit = false;
bool g_playing = true;
i32 g_fps = -1;
RuntimePlatform g_platform = compiledPlatform();
ConsoleProfile g_profile = ConsoleProfile::Unknown;
}  // namespace

// --- F6 a3 --- quit hooks
namespace {
constexpr u32 kMaxQuitHooks = 4;
Application::QuitHook g_quitHooks[kMaxQuitHooks] = {};
bool g_hooksRan = false;
}  // namespace
bool Application::addQuitHook(QuitHook hook) {
  if (!hook) return false;
  for (u32 i = 0; i < kMaxQuitHooks; ++i)
    if (g_quitHooks[i] == hook) return true;
  for (u32 i = 0; i < kMaxQuitHooks; ++i)
    if (!g_quitHooks[i]) { g_quitHooks[i] = hook; return true; }
  return false;
}
void Application::runQuitHooks() {
  if (g_hooksRan) return;
  g_hooksRan = true;
  for (u32 i = 0; i < kMaxQuitHooks; ++i)
    if (g_quitHooks[i]) g_quitHooks[i]();
}
// --- end F6 a3 ---

void Application::Quit() {
  g_quit = true;
  runQuitHooks();
}
bool Application::quitRequested() { return g_quit; }
bool Application::isPlaying() { return g_playing; }
void Application::setPlaying(bool playing) { g_playing = playing; }
i32 Application::targetFrameRate() { return g_fps; }
void Application::setTargetFrameRate(i32 fps) { g_fps = fps <= 0 ? -1 : fps; }
RuntimePlatform Application::platform() { return g_platform; }
void Application::setPlatform(RuntimePlatform p) { g_platform = p; }

// F6: an explicit setProfile wins; otherwise the console platforms imply their profile.
ConsoleProfile Application::profile() {
  if (g_profile != ConsoleProfile::Unknown) return g_profile;
  switch (g_platform) {
    case RuntimePlatform::Psp: return ConsoleProfile::Psp;
    case RuntimePlatform::Ps2: return ConsoleProfile::Ps2;
    case RuntimePlatform::Psx: return ConsoleProfile::Psx;
    default: return ConsoleProfile::Unknown;
  }
}
void Application::setProfile(ConsoleProfile p) { g_profile = p; }
const char* Application::profileName(ConsoleProfile p) {
  switch (p) {
    case ConsoleProfile::Psx: return "PSX";
    case ConsoleProfile::Psp: return "PSP";
    case ConsoleProfile::Ps2: return "PS2";
    case ConsoleProfile::Unknown: break;
  }
  return "Unknown";
}

const char* Application::platformName(RuntimePlatform p) {
  switch (p) {
    case RuntimePlatform::LinuxPlayer: return "LinuxPlayer";
    case RuntimePlatform::WindowsPlayer: return "WindowsPlayer";
    case RuntimePlatform::Android: return "Android";
    case RuntimePlatform::Psp: return "PSP";
    case RuntimePlatform::Ps2: return "PS2";
    case RuntimePlatform::Psx: return "PSX";
    case RuntimePlatform::Headless: return "Headless";
  }
  return "?";
}

bool Application::SetCpuClock(i32 mhz) {
  IPlatform* p = system_services::platform();
  return p && p->setCpuClock(mhz);
}
SystemLanguage Application::systemLanguage() {
  IPlatform* p = system_services::platform();
  return p ? static_cast<SystemLanguage>(p->language()) : SystemLanguage::Unknown;
}
bool Application::confirmButtonEast() {
  IPlatform* p = system_services::platform();
  return p && p->confirmButtonEast();
}

void Application::reset() {
  g_quit = false;
  g_playing = true;
  g_fps = -1;
  g_platform = compiledPlatform();
  g_profile = ConsoleProfile::Unknown;
  g_hooksRan = false;
}

}  // namespace rensga
