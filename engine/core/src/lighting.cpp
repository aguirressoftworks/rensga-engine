// F5: stub. Agent A2 implements gatherLights / selectLights (see lighting.h for the exact order rules).
#include "rensga/core/lighting.h"

namespace rensga {

void gatherLights(World&, LightSet& out) { out.count = 0; }
u32 selectLights(const LightSet&, const Vector3&, Scalar, u32, u8*) { return 0; }
u32 resolveMaxLights(const PackConsoleTargetExt* ctg2, RuntimePlatform platform) {
  if (ctg2 && ctg2->maxLightsPerObject) return ctg2->maxLightsPerObject;
  return platform == RuntimePlatform::Psp ? 4u : 8u;
}

}  // namespace rensga
