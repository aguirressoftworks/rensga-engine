// Custom component inspector registry (F6 E0). A function registered for a component type draws extra widgets under the
// reflected fields of that component in the Inspector (SpriteRenderer's sprite picker, Tilemap's palette buttons, later the
// Animator preview, Rigidbody constraints, ...). Designed so D3 can expose it to user projects: the callback only sees
// the document through ComponentRef/fieldValue/editFields, never raw scene JSON.
//
//   static void myInspector(App& a, const std::vector<ComponentRef>& refs, bool multi) { ... }
//   RENSGA_COMPONENT_INSPECTOR(my_sprite, "SpriteRenderer", myInspector)
//
// `refs` holds the same-type components of every selected object (refs[0] is the primary); `multi` is true when more than one
// object is selected. Several callbacks per type run in registration order. Edits must go through editFields().
#pragma once
#include <vector>

#include "rensga/editor/inspector_model.h"

namespace rensga {
namespace editor {

struct App;

using ComponentInspectorFn = void (*)(App&, const std::vector<ComponentRef>& refs, bool multi);

void registerComponentInspector(const char* typeName, ComponentInspectorFn fn);
bool hasComponentInspector(const char* typeName);
// Runs the callbacks registered for `typeName`; returns how many ran. (inspector.cpp reaches this through drawComponentExtras.)
int drawComponentInspectors(App& a, const std::string& typeName, const std::vector<ComponentRef>& refs, bool multi);

struct ComponentInspectorRegistrar {
  ComponentInspectorRegistrar(const char* typeName, ComponentInspectorFn fn) { registerComponentInspector(typeName, fn); }
};
#define RENSGA_COMPONENT_INSPECTOR(id, typeName, fn) static ::rensga::editor::ComponentInspectorRegistrar id##_inspector_{typeName, fn};

}  // namespace editor
}  // namespace rensga
