// F2.03 (Engine 3): asset importers and per-target cooking helpers. HOST ONLY.
// Importers turn source files (PNG, WAV, glTF) into generic in-memory forms; the target's
// ICookerFormatPlugin turns those into the pack-v2 payloads (see docs/rensga-engine/pack-format.md).
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "rensga/core/math.h"

namespace rensga {

struct PrimitiveMesh;

struct ImageRgba {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;  // width*height*4, R G B A
};

struct PcmAudio {
  uint32_t sampleRate = 0, channels = 0;
  std::vector<int16_t> samples;  // interleaved
  uint32_t frames() const { return channels ? static_cast<uint32_t>(samples.size() / channels) : 0; }
};

// Import settings read from the asset's .meta "importSettings" object.
struct TextureImport {
  std::string format = "auto";  // auto | rgba8 | clut4 | clut8
  int maxSize = 0;              // F3.02: longest edge in pixels; 0 = keep (larger images are scaled down, nearest)
  bool opaque = false;          // F3.02: "alpha": "opaque" drops the alpha channel (all 255)
  std::string filter = "bilinear";  // F3.02: point | bilinear. Stored for the runtime sampler; it does not change the cooked bytes.
};
struct AudioImport {
  bool mono = false;          // downmix stereo to mono
  bool halveRate = false;     // average frame pairs, sample rate / 2
  bool smallerAudio = false;  // ps2 shortcut for mono + halveRate
  bool loop = false;
  int64_t loopStart = 0;      // source frames
  int64_t loopEnd = -1;       // source frames, -1 = whole clip
  uint32_t sampleRate = 0;    // F3.02: resample down to this rate (Hz); 0 = keep
  bool streamed = false;      // F3.02: stored for the streaming path (files in Streaming/ today); does not change cooked bytes
};
struct ModelImport {          // F3.02: glTF import settings
  float scale = 1.0f;         // uniform scale applied to positions
  bool flatNormals = false;   // "normals": "flat" recomputes flat normals; "import" keeps the file's (default)
};

// A texture in a target's binary form (PackTexFormat values, see pack_format.h).
struct CookedTexture {
  uint32_t width = 0, height = 0;
  uint32_t format = 0;                // 0 RGBA8, 1 CLUT4, 2 CLUT8
  std::vector<uint8_t> pixels;
  std::vector<uint32_t> palette;      // RGBA8 entries (R in the low byte), 16 or 256 for CLUT
};

// ---- importers (error text is a sentence without the asset name; the caller adds it) ----
bool decodePng(const std::vector<uint8_t>& bytes, ImageRgba& out, std::string& error);
bool parseWav(const std::vector<uint8_t>& bytes, PcmAudio& out, std::string& error);
// Static glTF 2.0 / GLB meshes: node transforms applied, one triangle list, flat normals when absent.
// Vertex color comes from COLOR_0, else the material baseColorFactor. Textures are ignored for now.
bool importGltf(const std::filesystem::path& file, PrimitiveMesh& out, std::string& error);

// ---- shared texture/audio helpers (plugins call these) ----
CookedTexture makeRgba8(const ImageRgba& img);
// bits: 0 = pick 4 or 8 from the unique color count, 4 or 8 = force. Deterministic median cut
// when the image has more unique colors than the palette holds.
CookedTexture makeClut(const ImageRgba& img, int bits);
void audioDownmixMono(PcmAudio& pcm);
void audioResample(PcmAudio& pcm, uint32_t newRate);  // linear, only ever lowers the rate
void audioHalveRate(PcmAudio& pcm);

// ---- F3.02: import settings (shared by the cook and the editor's asset inspector) ----
// A .meta holds "importSettings" (all targets) and "targetOverrides": {"<target id>": {...}}. The settings a target
// cooks with are importSettings with the override's keys laid over them. No overrides = the same text as before.
std::string effectiveImportSettings(const nlohmann::json& meta, const std::string& target);
bool parseTextureImport(const std::string& jsonText, TextureImport& out, std::string& error);
bool parseAudioImport(const std::string& jsonText, AudioImport& out, std::string& error);
bool parseModelImport(const std::string& jsonText, ModelImport& out, std::string& error);
void applyModelImport(PrimitiveMesh& mesh, const ModelImport& s);

class ICookerFormatPlugin;
// Applies maxSize and alpha, then the target plugin's cookTexture. The one path the cook and the preview both use.
bool cookTextureImage(ICookerFormatPlugin& plugin, const ImageRgba& image, const TextureImport& settings, CookedTexture& out, std::string& error);

// Default 8x8 ASCII font (public-domain font8x8): every project gets it so text always works.
constexpr const char* kDefaultFontTextureGuid = "00000000-0000-4000-8000-00000000f0f0";
constexpr const char* kDefaultFontGuid = "00000000-0000-4000-8000-00000000f0f1";
ImageRgba defaultFontImage();  // 128x48, 16 columns x 6 rows of 8x8 cells, chars 32..127

}  // namespace rensga
