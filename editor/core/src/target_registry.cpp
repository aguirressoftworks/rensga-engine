#include "rensga/editor/target_module.h"

namespace rensga {
namespace editor {

static std::vector<TargetModule*>& modules() {
  static std::vector<TargetModule*> m;
  return m;
}

const std::vector<TargetModule*>& TargetRegistry::all() { return modules(); }
void TargetRegistry::add(TargetModule* module) { modules().push_back(module); }
TargetModule* TargetRegistry::find(std::string_view id) {
  for (TargetModule* m : modules())
    if (m->id() == id) return m;
  return nullptr;
}

}  // namespace editor
}  // namespace rensga
