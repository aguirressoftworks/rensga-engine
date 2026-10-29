#include "rensga/platform/ps2.h"

#include <time.h>

namespace rensga {

double Ps2Platform::timeSeconds() { return static_cast<double>(clock()) / static_cast<double>(CLOCKS_PER_SEC); }

}  // namespace rensga
