// F2.03: PC/Android cooker plugins. Textures stay RGBA8 and audio stays PCM16 (the shared defaults).
// These are platform targets: meshes use the project's console profile plugin.
#include "rensga/cooker/plugin.h"

namespace rensga {

template <int N>
class PlatformCookerPlugin final : public ICookerFormatPlugin {
 public:
  std::string_view id() const override { return N == 0 ? "linux" : (N == 1 ? "windows" : "android"); }
  bool isProfile() const override { return false; }
};
using LinuxCookerPlugin = PlatformCookerPlugin<0>;
using WindowsCookerPlugin = PlatformCookerPlugin<1>;
using AndroidCookerPlugin = PlatformCookerPlugin<2>;
RENSGA_REGISTER_COOKER_PLUGIN(LinuxCookerPlugin);
RENSGA_REGISTER_COOKER_PLUGIN(WindowsCookerPlugin);
RENSGA_REGISTER_COOKER_PLUGIN(AndroidCookerPlugin);

}  // namespace rensga
