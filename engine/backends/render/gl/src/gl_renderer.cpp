#include "rensga/render/gl.h"

#include <SDL.h>

#include "rensga/core/log.h"

namespace rensga {

namespace {
constexpr unsigned kColorBufferBit = 0x4000;
using ClearColorFn = void (*)(float, float, float, float);
using ClearFn = void (*)(unsigned);
using ViewportFn = void (*)(int, int, int, int);
}  // namespace

bool GlRenderer::init(u32 width, u32 height) {
  w_ = width;
  h_ = height;
  clearColor_ = SDL_GL_GetProcAddress("glClearColor");
  clear_ = SDL_GL_GetProcAddress("glClear");
  viewport_ = SDL_GL_GetProcAddress("glViewport");
  if (!clearColor_ || !clear_ || !viewport_) {
    RLOG_ERROR("gl", "missing GL entry points");
    return false;
  }
  RLOG_INFO("gl", "renderer ready %ux%u", width, height);
  return true;
}

void GlRenderer::beginFrame(Color32 c) {
  SDL_Window* win = SDL_GL_GetCurrentWindow();
  if (win) {
    int dw = 0, dh = 0;
    SDL_GL_GetDrawableSize(win, &dw, &dh);
    reinterpret_cast<ViewportFn>(viewport_)(0, 0, dw, dh);
  }
  reinterpret_cast<ClearColorFn>(clearColor_)(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
  reinterpret_cast<ClearFn>(clear_)(kColorBufferBit);
}

void GlRenderer::endFrame() {
  SDL_Window* win = SDL_GL_GetCurrentWindow();
  if (win) SDL_GL_SwapWindow(win);
}

}  // namespace rensga
