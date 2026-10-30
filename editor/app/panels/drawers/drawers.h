// F3.01: property drawers of the Inspector. A drawer shows one reflected field and returns true when the user edited it.
// The registry maps a FieldType (and optionally "<Type>.<field>") to a drawer, so a type or a single field can get its own.
#pragma once
#include <set>
#include <string>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "app.h"
#include "rensga/core/reflect.h"

namespace rensga {
namespace editor {

struct DrawCtx {
  App& a;
  const FieldDesc& f;
  std::string key;          // unique per object / component / field (keeps Euler angles stable while dragging)
  std::string ownerGuid;    // object that owns the component (UnityEvent defaults to it)
  bool mixed = false;       // multi-edit: the selected objects disagree, show "—" until the user edits
};

// `value` is what the scene holds (or the default). On an edit fill `out` and return true.
using FieldDrawer = bool (*)(DrawCtx&, const nlohmann::json& value, nlohmann::json& out);

void registerDrawer(FieldType t, FieldDrawer fn);
void registerFieldDrawer(const char* typeName, const char* fieldName, FieldDrawer fn);  // overrides the type drawer for one field
FieldDrawer drawerFor(const char* typeName, const FieldDesc& f);
void registerBuiltinDrawers();  // idempotent

// Shared widgets.
bool& inspectorGesture();  // a widget drag is in progress (one undo step for the whole drag)
bool eulerWidget(const std::string& key, float q[4], const char* label = "##rot", bool mixed = false);  // quaternion as Euler degrees
double q7(float v);        // shortest text a float round-trips through
const char* mixedText();   // what a mixed value shows

// Add Component popup (drawers/add_component.cpp): fuzzy search, categories, recently used. Adds to every object in `guids`
// as one undo step; required components come first. `allowed` is the target's component list (empty = no limit).
void drawAddComponentPopup(App& a, const char* popupId, const std::vector<std::string>& guids, const std::set<std::string>& allowed,
                           const std::string& targetName);

// --ui-event verbs: multiselect:A|B, addcomp:<Type>, rmcomp:<i>, moveup:<i>, movedown:<i>, resetcomp:<i>, copycomp:<i>,
// pastecomp:<i>, ctxmenu:<i>|<method>, setfield:<Type>.<field>=<json>, setobj:<name|active|tag|layer>=<json>, undo, redo.
bool inspectorHook(App& a, const std::string& act, const std::string& arg);

// Reference drawer helpers (drawers/reference.cpp).
std::string guidLabel(App& a, const std::string& guid, bool* known = nullptr);  // "Name (object)", "path (asset)", "None", "Missing ..."
// A scene object or project asset picker: a popup listing both, with search. Returns true and fills `out` on a pick.
bool guidPicker(App& a, const char* id, const std::string& cur, const char* fieldName, bool wantObjects, bool wantAssets, std::string& out);
// Accepts RENSGA_OBJECT and RENSGA_ASSET_GUID drops on the last item.
bool guidDropTarget(App& a, bool wantObjects, bool wantAssets, std::string& out);

}  // namespace editor
}  // namespace rensga
