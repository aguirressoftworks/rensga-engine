#include "rensga/cooker/plugin.h"

namespace rensga {

CookerPluginRegistry& CookerPluginRegistry::instance() {
  static CookerPluginRegistry r;
  return r;
}
void CookerPluginRegistry::add(ICookerFormatPlugin* plugin) { plugins_.push_back(plugin); }
ICookerFormatPlugin* CookerPluginRegistry::find(std::string_view id) const {
  for (ICookerFormatPlugin* p : plugins_)
    if (p->id() == id) return p;
  return nullptr;
}

}  // namespace rensga
