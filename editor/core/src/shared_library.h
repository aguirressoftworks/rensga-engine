// Tiny dlopen / LoadLibrary shim for the game module (editor core, private).
#pragma once
#include <string>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace rensga {
namespace editor {
namespace shlib {

inline void* open(const std::string& path, std::string& err) {
#if defined(_WIN32)
  HMODULE h = LoadLibraryA(path.c_str());
  if (!h) err = "LoadLibrary failed (error " + std::to_string(GetLastError()) + "): " + path;
  return h;
#else
  dlerror();
  void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!h) { const char* e = dlerror(); err = e ? e : "dlopen failed"; }
  return h;
#endif
}

inline void close(void* h) {
  if (!h) return;
#if defined(_WIN32)
  FreeLibrary(static_cast<HMODULE>(h));
#else
  dlclose(h);
#endif
}

}  // namespace shlib
}  // namespace editor
}  // namespace rensga
