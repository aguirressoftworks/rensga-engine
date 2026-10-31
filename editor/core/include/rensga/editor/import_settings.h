// F3.02: import settings schema and the importer. The schema (which fields an asset kind has, which values a build target
// accepts) is data: the texture formats come from the ConsoleTarget. The importer reuses the cooker: a reimport runs
// cookAssets on one asset through the per-asset cache in Library/Cache/<target>/, the same entries a full cook uses.
// No ImGui, no console names.
#pragma once
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/cooker/cooker.h"
#include "rensga/editor/asset_database.h"
#include "rensga/editor/target_module.h"

namespace rensga {
namespace editor {

struct ImportField {
  std::string key, label;
  std::string type;                  // "enum" | "int" | "float" | "bool"
  std::vector<std::string> options;  // enum values
  nlohmann::json def;                // value when the key is absent
  std::string help;
};

// Build target ids a project cooks for: its console target first, then the platform builds. These are the tabs of the
// asset inspector and the keys of "targetOverrides".
std::vector<std::string> buildTargetIds(const std::string& projectTarget);

// Fields of an asset kind for one build target. `console` is the project's ConsoleTarget; `isProjectTarget` selects its
// texture formats and audio rate, other build targets accept RGBA8 only. Empty for kinds without import settings.
// `rules` (F5.06, ImportRules of the target: effectiveImportRules) adds the target's limits: the longest-edge options stop at its
// maxTextureSize, the sample-rate options include its rates, and a Model gets "vertexPrecision" when the target offers
// more than float. Without it the schema is the F3.02 one.
std::vector<ImportField> importFields(AssetKind kind, const ConsoleTargetData& console, bool isProjectTarget, const ImportRules* rules = nullptr);

// Value of a field in `settings` (the default when absent).
nlohmann::json fieldValue(const ImportField& f, const nlohmann::json& settings);

struct ImportOutcome {
  bool ok = false;
  std::string error;
  AssetKind kind = AssetKind::Other;
  bool fromCache = false;
  // textures
  CookedTexture tex;
  uint32_t srcWidth = 0, srcHeight = 0;
  uint32_t storedWidth = 0, storedHeight = 0;  // size in memory (padded to a power of two where the target needs it)
  // textures and audio
  uint64_t cookedBytes = 0;     // pixels + palette, or PCM bytes
  uint32_t audioRate = 0, audioChannels = 0, audioFrames = 0;
};

class AssetImporter {
 public:
  void open(const std::filesystem::path& projectRoot) { root_ = projectRoot; }
  // Cooks one texture or audio asset for `target` with `settingsText` (cooker text, see effectiveImportSettings).
  // useCache: read and write Library/Cache/<target>/ (a reimport); false: preview of unsaved settings, nothing is stored.
  ImportOutcome import(const AssetEntry& asset, const std::string& target, const std::string& settingsText, bool useCache) const;

 private:
  std::filesystem::path root_;
};

// F5.06: what one asset costs the target, from the cooker's own numbers. `notes` are the warnings the import tab lists
// (a texture the cook will downscale, a size that is not a power of two, a skin that needs batch splitting).
struct AssetCost {
  bool valid = false;
  uint64_t vramBytes = 0;  // video memory the asset occupies once loaded
  uint64_t ramBytes = 0;   // main memory (the pack is kept in RAM; meshes and clips are read from it)
  std::string detail;      // one line: "128 x 128 -> 128 x 128 stored, 16 colours, 4-bit"
  std::vector<std::string> notes;
};
// Texture: from the cooked outcome of the target's plugin (AssetImporter::import).
AssetCost textureCost(const ImportOutcome& o, const ImportRules& rules);
// Audio: PCM bytes after the import settings.
AssetCost audioCost(const ImportOutcome& o);
// Model (glTF): reads the file with the cooker's importer; vertex bytes follow `vertexPrecision` ("float" or "16bit") when the
// target can pack compact vertices (`compactCapable`), batches follow maxBonesPerDraw.
AssetCost modelCost(const std::filesystem::path& file, const ConsoleTargetData& console, const std::string& vertexPrecision, bool compactCapable);

// RGBA8 pixels of a cooked texture (palette expanded) for display. width*height*4 bytes.
std::vector<uint8_t> cookedTextureToRgba(const CookedTexture& t);
// "16 colours" / "256 colours" / "RGBA8"
std::string cookedTextureDescription(const CookedTexture& t);

}  // namespace editor
}  // namespace rensga
