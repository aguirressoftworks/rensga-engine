#include "rensga/platform/sdl.h"

#include <SDL.h>

#include "rensga/core/log.h"

namespace rensga {

bool SdlPlatform::init() {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
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
  RLOG_INFO("sdl", "window + GL context ready");
  return true;
}

void SdlPlatform::shutdown() {
  if (gl_) SDL_GL_DeleteContext(static_cast<SDL_GLContext>(gl_));
  if (window_) SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
  gl_ = nullptr;
  window_ = nullptr;
  SDL_Quit();
}

void SdlPlatform::pollInput(VirtualGamepad& pad) {
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) quit_ = true;
    if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) quit_ = true;
  }
  pad = VirtualGamepad();
}

double SdlPlatform::timeSeconds() { return static_cast<double>(SDL_GetPerformanceCounter()) / static_cast<double>(SDL_GetPerformanceFrequency()); }

}  // namespace rensga
