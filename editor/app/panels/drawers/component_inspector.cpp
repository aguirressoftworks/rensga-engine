#include "component_inspector.h"

#include <map>

#include "app.h"
#include "panels/tile_tools.h"
#include "extensions/ext_host.h"  // F6 D3: inspectors registered by the user project

namespace rensga {
namespace editor {

namespace {
std::map<std::string, std::vector<ComponentInspectorFn>>& table() {
  static std::map<std::string, std::vector<ComponentInspectorFn>> t;
  return t;
}
}  // namespace

void registerComponentInspector(const char* typeName, ComponentInspectorFn fn) {
  if (typeName && fn) table()[typeName].push_back(fn);
}

bool hasComponentInspector(const char* typeName) { return typeName && table().count(typeName) != 0; }

int drawComponentInspectors(App& a, const std::string& typeName, const std::vector<ComponentRef>& refs, bool multi) {
  if (refs.empty()) return 0;
  auto it = table().find(typeName);
  int ran = 0;
  if (it != table().end()) {
    for (ComponentInspectorFn fn : it->second) fn(a, refs, multi);
    ran = static_cast<int>(it->second.size());
  }
  ran += runExtensionInspectors(a, typeName, refs, multi);  // F6 D3: RENSGA_CUSTOM_INSPECTOR in <project>/Editor
  return ran;
}

// Declared in panels/tile_tools.h (F4.08); inspector.cpp calls it after the reflected fields.
void drawComponentExtras(App& a, const std::string& type, const std::vector<ComponentRef>& refs, bool multi) {
  drawComponentInspectors(a, type, refs, multi);
}

}  // namespace editor
}  // namespace rensga
