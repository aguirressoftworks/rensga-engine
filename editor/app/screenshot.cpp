#include <SDL_opengl.h>

#include <algorithm>
#include <vector>

#include "app.h"

// stb_image_write is implemented once, in engine/backends/render/gl (linked into the editor for the Game view).
#include "stb_image_write.h"

namespace rensga {
namespace editor {

bool writeScreenshotPng(const char* file, int w, int h) {
  std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4), flipped(px.size());
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  for (int y = 0; y < h; ++y)
    std::copy(px.begin() + static_cast<size_t>(h - 1 - y) * w * 4, px.begin() + static_cast<size_t>(h - y) * w * 4,
              flipped.begin() + static_cast<size_t>(y) * w * 4);
  for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
  return stbi_write_png(file, w, h, 4, flipped.data(), w * 4) != 0;
}

}  // namespace editor
}  // namespace rensga
