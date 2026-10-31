// F4.05 (PSP Engine 3): PSP texture and mesh forms. HOST ONLY. The cooker plugin and the tests call these.
// Rules live in docs/rensga-engine/pack-format.md ("PSP textures", "Compact meshes").
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "rensga/cooker/assets.h"
#include "rensga/core/pack_format.h"

namespace rensga {

constexpr uint32_t kPspMaxTextureSize = 512;     // GU texture edge limit
constexpr uint32_t kPspSwizzleBlockBytes = 16;   // block width in bytes
constexpr uint32_t kPspSwizzleBlockRows = 8;     // block height in rows

// Smallest power of two >= v (v >= 1).
uint32_t pspPow2AtLeast(uint32_t v);
// Minimum stored width in pixels for a PackTexFormat (32 CLUT4, 16 CLUT8, 8 for the 16- and 32-bit formats).
uint32_t pspMinStoredWidth(uint32_t format);
// Bits per stored pixel for a PackTexFormat (4, 8, 16, 16, 32 for CLUT4, CLUT8, 5551, 4444, RGBA8).
uint32_t pspBitsPerPixel(uint32_t format);

// Swizzles `pitchBytes` x `rows` linear bytes into 16-byte x 8-row blocks: bands top to bottom, blocks left to right,
// rows inside a block top to bottom. pitchBytes % 16 == 0 and rows % 8 == 0 (the stored size rules guarantee both).
void pspSwizzle(const uint8_t* linear, uint32_t pitchBytes, uint32_t rows, uint8_t* out);
void pspUnswizzle(const uint8_t* swizzled, uint32_t pitchBytes, uint32_t rows, uint8_t* out);

// Cooks one texture for the PSP: format by name (auto, clut4, clut8, rgba5551, rgba4444, rgba8), padded to the stored size
// with zero bytes, swizzled, flags = kTexSwizzled. `error` names the problem and the fix (no asset name).
bool pspCookTexture(const ImageRgba& image, const std::string& format, CookedTexture& out, std::string& error);

// Compact GU meshes (MSHX format 1). `floatVerts` holds PackVertex records; the result holds PackVertexCompact records
// sharing one offset and one power-of-two scale per axis. position = offset + xyz * scale.
bool pspCompactVertices(const std::vector<uint8_t>& floatVerts, std::vector<uint8_t>& out, PackMeshExt& ext, std::string& error);

}  // namespace rensga
