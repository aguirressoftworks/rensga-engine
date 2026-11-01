// F6.02 (Editor 8): the material document behind the Material Editor. A .mat.json asset (docs/rensga-engine/pack-format.md,
// "Material asset": texture GUID, color, unlit, alphaTest) edited with the profile's limits applied: what the target cannot draw is
// refused here and greyed out (with its reason) by the UI. Console-agnostic: every rule is ConsoleTarget data (features, limits).
//
//   blend mode   opaque or alpha blended. The renderers blend when the material color's alpha is below 255, so the mode is the alpha:
//                Opaque pins it to 255, Alpha blended keeps what the artist set
//   alpha test   pixels with alpha below 50% are discarded
//   unlit        the vertex colors (baked light) x texture x color only; forced when the target has no "lighting" feature
//   fog          taken from the scene's RenderSettings by every lit or unlit material; shown (not editable) with the target's support
// No ImGui, no GL.
#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/cooker/cooker.h"

namespace rensga {
namespace editor {

struct MaterialRules {
  bool lighting = true;      // the target draws lit materials ("lighting" feature)
  bool fog = true;           // the target draws fog ("fog" feature)
  bool blend = true;         // alpha blending (always on today; a target can switch it off with features that omit "blend" once it lists any)
  uint32_t maxTextureSize = 0;
  std::string noLightingReason, noFogReason, noBlendReason;
};
// features / limits of the ConsoleTarget. An empty feature list means "every feature" (an older ConsoleTarget.json).
MaterialRules materialRulesFor(const ConsoleTargetData& ct);

enum class BlendMode { Opaque = 0, Alpha = 1 };

struct MaterialData {
  std::string texture;                    // asset GUID text, "" = none
  std::array<int, 4> color{255, 255, 255, 255};
  bool unlit = false, alphaTest = false;
  BlendMode blend() const { return color[3] < 255 ? BlendMode::Alpha : BlendMode::Opaque; }
  bool operator==(const MaterialData& o) const { return texture == o.texture && color == o.color && unlit == o.unlit && alphaTest == o.alphaTest; }
};

class MaterialDocument {
 public:
  // Reads a .mat.json (unknown keys are kept on save). False and `err` on a parse error.
  bool load(const std::filesystem::path& file, std::string& err);
  // Writes the file (atomic: temp file + rename). Keeps keys this version does not know.
  bool save(std::string& err);
  const std::filesystem::path& path() const { return path_; }
  const MaterialData& data() const { return data_; }
  bool dirty() const { return !(data_ == saved_); }

  // Every setter applies the rules first and returns false (leaving the data alone) when the target refuses the value. One undo step each.
  bool setTexture(const std::string& guid);
  bool setColor(int r, int g, int b);                       // rgb only; the alpha belongs to the blend mode
  bool setBlend(BlendMode m, const MaterialRules& rules);   // Alpha keeps the current alpha, or 160 when it was 255
  bool setAlpha(int a, const MaterialRules& rules);         // < 255 needs blending
  bool setUnlit(bool on, const MaterialRules& rules);
  bool setAlphaTest(bool on);
  // Brings the data inside the rules (a document loaded for a target that cannot light or blend). Returns true when it changed anything.
  bool normalize(const MaterialRules& rules);

  bool undo();
  bool redo();
  size_t undoDepth() const { return undo_.size(); }

 private:
  void push();
  std::filesystem::path path_;
  nlohmann::ordered_json raw_ = nlohmann::ordered_json::object();
  MaterialData data_, saved_;
  std::vector<MaterialData> undo_, redo_;
};

}  // namespace editor
}  // namespace rensga
