// F4.05: PSP texture padding, swizzle and compact vertices. See include/rensga/cooker/psp_format.h.
#include "rensga/cooker/psp_format.h"
#include "rensga/cooker/model3d.h"

#include <math.h>
#include <string.h>

#include <algorithm>

namespace rensga {

uint32_t pspPow2AtLeast(uint32_t v) {
  uint32_t p = 1;
  while (p < v) p <<= 1;
  return p;
}

uint32_t pspMinStoredWidth(uint32_t format) {
  switch (format) {
    case static_cast<uint32_t>(PackTexFormat::CLUT4): return 32;
    case static_cast<uint32_t>(PackTexFormat::CLUT8): return 16;
    default: return 8;
  }
}

uint32_t pspBitsPerPixel(uint32_t format) {
  switch (format) {
    case static_cast<uint32_t>(PackTexFormat::CLUT4): return 4;
    case static_cast<uint32_t>(PackTexFormat::CLUT8): return 8;
    case static_cast<uint32_t>(PackTexFormat::RGBA5551):
    case static_cast<uint32_t>(PackTexFormat::RGBA4444): return 16;
    default: return 32;
  }
}

void pspSwizzle(const uint8_t* linear, uint32_t pitchBytes, uint32_t rows, uint8_t* out) {
  const uint32_t cols = pitchBytes / kPspSwizzleBlockBytes, bands = rows / kPspSwizzleBlockRows;
  for (uint32_t by = 0; by < bands; ++by)
    for (uint32_t bx = 0; bx < cols; ++bx)
      for (uint32_t r = 0; r < kPspSwizzleBlockRows; ++r) {
        memcpy(out, linear + static_cast<size_t>(by * kPspSwizzleBlockRows + r) * pitchBytes + bx * kPspSwizzleBlockBytes,
               kPspSwizzleBlockBytes);
        out += kPspSwizzleBlockBytes;
      }
}

void pspUnswizzle(const uint8_t* swizzled, uint32_t pitchBytes, uint32_t rows, uint8_t* out) {
  const uint32_t cols = pitchBytes / kPspSwizzleBlockBytes, bands = rows / kPspSwizzleBlockRows;
  for (uint32_t by = 0; by < bands; ++by)
    for (uint32_t bx = 0; bx < cols; ++bx)
      for (uint32_t r = 0; r < kPspSwizzleBlockRows; ++r) {
        memcpy(out + static_cast<size_t>(by * kPspSwizzleBlockRows + r) * pitchBytes + bx * kPspSwizzleBlockBytes, swizzled,
               kPspSwizzleBlockBytes);
        swizzled += kPspSwizzleBlockBytes;
      }
}

bool pspCookTexture(const ImageRgba& imageIn, const std::string& fmt, CookedTexture& out, std::string& error) {
  // F5: an image the GU cannot hold is scaled down (box filter) to the texture limit instead of failing; the cooked record's
  // width / height are the scaled size and the cook reports the change.
  ImageRgba scaled;
  const ImageRgba* use = &imageIn;
  if (imageIn.width > kPspMaxTextureSize || imageIn.height > kPspMaxTextureSize) {
    const double k = static_cast<double>(kPspMaxTextureSize) / std::max(imageIn.width, imageIn.height);
    scaled = boxDownscale(imageIn, std::max(1u, static_cast<uint32_t>(imageIn.width * k)), std::max(1u, static_cast<uint32_t>(imageIn.height * k)));
    use = &scaled;
  }
  const ImageRgba& image = *use;
  CookedTexture src;  // source-sized pixels in the target format, not yet padded or swizzled
  uint32_t format = 0;
  if (fmt == "auto") src = makeClut(image, 0);      // the PS2 palette code, shared
  else if (fmt == "clut4") src = makeClut(image, 4);
  else if (fmt == "clut8") src = makeClut(image, 8);
  else if (fmt == "rgba8") src = makeRgba8(image);
  else if (fmt == "rgba5551" || fmt == "rgba4444") {
    src.width = image.width;
    src.height = image.height;
    const bool is5551 = fmt == "rgba5551";
    src.format = static_cast<uint32_t>(is5551 ? PackTexFormat::RGBA5551 : PackTexFormat::RGBA4444);
    src.pixels.resize(static_cast<size_t>(image.width) * image.height * 2);
    for (size_t i = 0, n = static_cast<size_t>(image.width) * image.height; i < n; ++i) {
      const uint8_t* p = &image.rgba[i * 4];
      uint16_t v;
      if (is5551) {
        v = static_cast<uint16_t>(((p[0] * 31 + 127) / 255) | (((p[1] * 31 + 127) / 255) << 5) | (((p[2] * 31 + 127) / 255) << 10) |
                                  (p[3] >= 128 ? 0x8000 : 0));
      } else {
        v = static_cast<uint16_t>(((p[0] * 15 + 127) / 255) | (((p[1] * 15 + 127) / 255) << 4) | (((p[2] * 15 + 127) / 255) << 8) |
                                  (((p[3] * 15 + 127) / 255) << 12));
      }
      src.pixels[i * 2] = static_cast<uint8_t>(v & 255);
      src.pixels[i * 2 + 1] = static_cast<uint8_t>(v >> 8);
    }
  } else {
    error = "unknown texture format '" + fmt + "' (auto, clut4, clut8, rgba5551, rgba4444, rgba8)";
    return false;
  }
  format = src.format;

  const uint32_t w = image.width, h = image.height;
  const uint32_t sw = std::max(pspPow2AtLeast(w), pspMinStoredWidth(format)), sh = std::max(pspPow2AtLeast(h), kPspSwizzleBlockRows);
  if (sw > kPspMaxTextureSize || sh > kPspMaxTextureSize) {
    error = "texture is " + std::to_string(w) + "x" + std::to_string(h) + ", which the PSP stores as " + std::to_string(sw) + "x" +
            std::to_string(sh) + "; the limit is " + std::to_string(kPspMaxTextureSize) + "x" + std::to_string(kPspMaxTextureSize) +
            ". Set maxSize to " + std::to_string(kPspMaxTextureSize) + " or less in the asset's import settings";
    return false;
  }

  const uint32_t bits = pspBitsPerPixel(format);
  const uint32_t pitch = sw * bits / 8;
  std::vector<uint8_t> linear(static_cast<size_t>(pitch) * sh, 0);  // padding stays zero
  if (bits == 4) {
    const size_t srcStride = (w + 1) / 2;
    for (uint32_t y = 0; y < h; ++y) memcpy(&linear[static_cast<size_t>(y) * pitch], &src.pixels[y * srcStride], srcStride);
  } else {
    const size_t rowBytes = static_cast<size_t>(w) * bits / 8;
    for (uint32_t y = 0; y < h; ++y) memcpy(&linear[static_cast<size_t>(y) * pitch], &src.pixels[y * rowBytes], rowBytes);
  }

  out = CookedTexture();
  out.width = w;
  out.height = h;
  out.format = format;
  out.storedWidth = sw;
  out.storedHeight = sh;
  out.flags = kTexSwizzled;
  out.palette = std::move(src.palette);
  out.pixels.resize(linear.size());
  pspSwizzle(linear.data(), pitch, sh, out.pixels.data());
  return true;
}

bool pspCompactVertices(const std::vector<uint8_t>& floatVerts, std::vector<uint8_t>& out, PackMeshExt& ext, std::string& error) {
  if (floatVerts.size() % sizeof(PackVertex) != 0) {
    error = "vertex bytes are not a whole number of PackVertex records";
    return false;
  }
  const size_t n = floatVerts.size() / sizeof(PackVertex);
  ext = PackMeshExt();
  ext.format = kMeshFormatCompact16;
  if (n == 0) return false;
  const PackVertex* v = reinterpret_cast<const PackVertex*>(floatVerts.data());
  float lo[3] = {v[0].x, v[0].y, v[0].z}, hi[3] = {v[0].x, v[0].y, v[0].z};
  for (size_t i = 1; i < n; ++i) {
    const float p[3] = {v[i].x, v[i].y, v[i].z};
    for (int k = 0; k < 3; ++k) {
      lo[k] = std::min(lo[k], p[k]);
      hi[k] = std::max(hi[k], p[k]);
    }
  }
  for (int k = 0; k < 3; ++k) {
    ext.offset[k] = 0.5f * (lo[k] + hi[k]);
    const float half = std::max(hi[k] - ext.offset[k], ext.offset[k] - lo[k]);
    // Smallest power of two with half / scale <= 32767.
    float scale = 1.0f;
    if (half > 0.0f) {
      int e = 0;
      frexpf(half / 32767.0f, &e);          // half/32767 = m * 2^e, m in [0.5, 1)
      scale = ldexpf(1.0f, e);              // 2^e >= half/32767
      if (scale * 32767.0f < half) scale *= 2.0f;
    }
    ext.scale[k] = scale;
  }
  out.assign(n * sizeof(PackVertexCompact), 0);
  PackVertexCompact* o = reinterpret_cast<PackVertexCompact*>(out.data());
  for (size_t i = 0; i < n; ++i) {
    const float p[3] = {v[i].x, v[i].y, v[i].z};
    int16_t q[3];
    for (int k = 0; k < 3; ++k) {
      float f = roundf((p[k] - ext.offset[k]) / ext.scale[k]);
      q[k] = static_cast<int16_t>(std::max(-32767.0f, std::min(32767.0f, f)));
    }
    o[i].color = v[i].color;
    o[i].x = q[0]; o[i].y = q[1]; o[i].z = q[2];
    o[i].pad = 0;
  }
  return true;
}

}  // namespace rensga
