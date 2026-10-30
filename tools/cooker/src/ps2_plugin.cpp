// F2.03: PS2 cooker plugin. Textures become CLUT4/CLUT8 (the GS has 4 MB of VRAM), chosen by unique
// color count or the "format" import setting; audio can shrink with "smallerAudio" (mono + 22050 Hz
// halving). Meshes arrive with the PS2 3D roadmap phases.
#include "rensga/cooker/plugin.h"

namespace rensga {

class Ps2CookerPlugin final : public ICookerFormatPlugin {
 public:
  std::string_view id() const override { return "ps2"; }
  bool cookTexture(const ImageRgba& image, const TextureImport& s, CookedTexture& out, std::string& error) override {
    if (s.format == "rgba8") out = makeRgba8(image);
    else if (s.format == "clut4") out = makeClut(image, 4);
    else if (s.format == "clut8") out = makeClut(image, 8);
    else if (s.format == "auto") out = makeClut(image, 0);
    else {
      error = "unknown texture format '" + s.format + "' (auto, rgba8, clut4, clut8)";
      return false;
    }
    return true;
  }
  bool cookAudio(PcmAudio& pcm, const AudioImport& s, std::string& error) override {
    AudioImport a = s;
    if (s.smallerAudio) a.mono = a.halveRate = true;
    return ICookerFormatPlugin::cookAudio(pcm, a, error);
  }
};
RENSGA_REGISTER_COOKER_PLUGIN(Ps2CookerPlugin);

}  // namespace rensga
