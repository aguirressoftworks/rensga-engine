// F3.01 (Editor 2) + F3.07 (API 11): the inspector's logic without ImGui, so it can be tested and every edit path is one
// undo step. The panel (editor/app/panels/inspector.cpp and drawers/) only draws and calls these functions.
//
// Editor callbacks (OnValidate, Reset, CONTEXT_MENU methods) run on a TEMPORARY instance: it is built through the
// TypeInfo into an aligned buffer, the component's JSON fields are applied, the callback runs, and the fields that
// changed are written back in the SAME undo step as the edit that caused it. The instance has no World and no
// gameObject (null/detached), so a callback may only touch the component's own fields.
// No console names here: allowed-component lists come in as data.
#pragma once
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/core/reflect.h"
#include "rensga/editor/scene_document.h"

namespace rensga {
namespace editor {

// ---- reflection helpers -------------------------------------------------------------------------------------
nlohmann::json typeDefaults(const TypeInfo& ti);                       // reflected defaults (fieldsToJson of a fresh instance)
std::string prettyFieldName(const char* name);                         // maxHealth -> "Max Health"

enum class EditorCallback { Validate, Reset };
// Runs a callback on a temporary instance. `fields` is the component's stored JSON (absent fields take their default).
// Returns the fields to store: the input with only the fields the callback changed rewritten. `ran` (optional) tells
// whether the type has such a callback at all (non-Behaviour components have none).
nlohmann::json runEditorCallback(const TypeInfo& ti, EditorCallback cb, const nlohmann::json& fields, bool* ran = nullptr);
// Same for a CONTEXT_MENU method (void()). Returns the input unchanged when the method is missing.
nlohmann::json runContextMethod(const TypeInfo& ti, const std::string& method, const nlohmann::json& fields);

// ---- component clipboard (Copy / Paste Component Values) ---------------------------------------------------
// Lives in the editor process, so it survives play mode and Stop.
struct ComponentClipboard {
  bool has = false;
  std::string type;
  nlohmann::json fields = nlohmann::json::object();
};
ComponentClipboard& componentClipboard();

// ---- Add Component search ------------------------------------------------------------------------------------
// Case-insensitive fuzzy match: -1 = no match, higher is better (prefix, word starts and runs score more).
int fuzzyScore(const std::string& query, const std::string& text);
std::string componentCategory(const TypeInfo& ti);                     // "Rendering", "Physics", "Audio", "UI", "Scripts", "Other"
std::vector<std::string> recentComponents();                           // most recent first, at most 8
void noteComponentUsed(const std::string& typeName);                   // also saved next to the editor preferences
void resetRecentComponents();                                          // tests

// ---- rules ----------------------------------------------------------------------------------------------------
// Types REQUIRE_COMPONENT asks for that the object does not have yet (transitive, in dependency order).
std::vector<std::string> missingRequirements(const TypeInfo& ti, const SceneObject& o);
// "" when `type` can be added; else the reason (not in the ConsoleTarget list, or a required type is not).
// `allowed` empty = no restriction.
std::string addBlockReason(const std::string& type, const SceneObject& o, const std::set<std::string>& allowed, const std::string& targetName);
// "" when component `idx` can be removed; else "Required by <Type>".
std::string removeBlockReason(const SceneObject& o, int idx);
// Why a component is greyed ("Not supported by <target>"), or "".
std::string greyReason(const std::string& type, const std::set<std::string>& allowed, const std::string& targetName);
bool isMissingScript(const std::string& type);                         // type not registered (script not loaded)

// ---- document operations: each one is exactly one undo step -----------------------------------------------------
struct OpResult {
  bool ok = false;
  std::string reason;
  explicit operator bool() const { return ok; }
};

// Adds required components first, then the component with its defaults after Reset(). Records it as recently used.
OpResult addComponentTo(SceneDocument& d, const std::string& guid, const std::string& type, const std::set<std::string>& allowed = {},
                        const std::string& targetName = "target");
OpResult removeComponentFrom(SceneDocument& d, const std::string& guid, int idx);
OpResult moveComponentBy(SceneDocument& d, const std::string& guid, int idx, int delta);   // -1 up, +1 down
OpResult resetComponent(SceneDocument& d, const std::string& guid, int idx);                // defaults, then Reset()
OpResult copyComponentValues(const SceneDocument& d, const std::string& guid, int idx);
OpResult pasteComponentValues(SceneDocument& d, const std::string& guid, int idx);          // same type only
OpResult runContextMenu(SceneDocument& d, const std::string& guid, int idx, const std::string& method);

// A component of the selection that a multi-edit touches.
struct ComponentRef {
  std::string guid;
  int index = 0;
};
// For each object of the selection the first component named `type`; empty when one object lacks it.
std::vector<ComponentRef> sameTypeAcross(const SceneDocument& d, const std::vector<std::string>& guids, const std::string& type);
// Types every selected object has (order of the first object).
std::vector<std::string> commonComponentTypes(const SceneDocument& d, const std::vector<std::string>& guids);
// Effective value (stored or default) of a field.
nlohmann::json fieldValue(const SceneDocument& d, const ComponentRef& r, const std::string& field);
// True when the refs disagree on a field (shown as "—").
bool fieldMixed(const SceneDocument& d, const std::vector<ComponentRef>& refs, const std::string& field);
// Sets one field on every ref, then runs OnValidate on each, all in one undo step.
OpResult editFields(SceneDocument& d, const std::vector<ComponentRef>& refs, const std::string& field, const nlohmann::json& value);
// Name / active / tag / layer on every object of the selection, one undo step.
OpResult editObjects(SceneDocument& d, const std::vector<std::string>& guids, const std::string& what, const nlohmann::json& value);

}  // namespace editor
}  // namespace rensga
