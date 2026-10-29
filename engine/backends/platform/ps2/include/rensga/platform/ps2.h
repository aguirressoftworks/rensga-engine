// PS2 platform backend (EE): clock via newlib; pad input (padlib + IRX) lands with Engine 2 / F8.
#pragma once
#include "rensga/core/backends.h"

namespace rensga {

class Ps2Platform : public IPlatform {
 public:
  bool init() override { return true; }
  void shutdown() override {}
  void pollInput(VirtualGamepad& pad) override { pad = VirtualGamepad(); }
  double timeSeconds() override;
  bool shouldQuit() override { return false; }
};

}  // namespace rensga
