// MVP-47: the editable scene. The scene JSON (ordered, so saves keep key order and diffs small) is the
// source of truth; scene() is a typed view rebuilt after every change. Undo/redo are commands on an UndoStack (JSON snapshot commands).
// No ImGui, no console names.
#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/cooker/prefab_expand.h"
#include "rensga/editor/project.h"
#include "rensga/editor/undo.h"

namespace rensga {
namespace editor {

class PrefabLibrary;

std::string generateGuid();  // random (v4) GUID, lowercase 8-4-4-4-12

// F3.04: while a play session runs, edits to transforms, active flags and component fields go here (the running World)
// instead of the document. Everything else is refused: the document is frozen until Stop.
class PlayEditSink {
 public:
  virtual ~PlayEditSink() = default;
  virtual bool transform(const std::string& guid, const float pos[3], const float rot[4], const float scale[3]) = 0;
  virtual bool active(const std::string& guid, bool active) = 0;
  virtual bool field(const std::string& guid, int component, const std::string& field, const nlohmann::json& value) = 0;
};

class SceneDocument {
 public:
  static constexpr size_t kMaxUndo = UndoStack::kDefaultLimit;

  bool load(const std::filesystem::path& file, std::string& err);
  bool save(std::string& err);  // writes to path()
  bool loaded() const { return loaded_; }
  const std::filesystem::path& path() const { return path_; }
  bool dirty() const { return forcedDirty_ || stack_.dirty(); }
  const Scene& scene() const { return live(); }
  const nlohmann::ordered_json& document() const { return doc_; }
  // Replaces the document with `j` as if loaded from `file` (editor restart): undo history is dropped, dirty is kept.
  bool restore(const std::filesystem::path& file, nlohmann::ordered_json j, bool dirty);
  const SceneObject* find(const std::string& guid) const;
  int indexOf(const std::string& guid) const;  // index in scene().objects or -1
  std::vector<std::string> childrenOf(const std::string& parentGuid) const;  // "" = roots, in file order
  bool isDescendant(const std::string& guid, const std::string& ancestor) const;

  // Selection (by GUID). Not part of undo; cleared when the object disappears.
  // selected() is the primary object (the last one clicked); selection() is every selected object in click order.
  const std::string& selected() const { return selected_; }
  void select(const std::string& guid);                  // only this object ("" clears)
  // Replaces the selection; unknown GUIDs are dropped and the LAST one becomes the primary (selected()).
  void setSelection(const std::vector<std::string>& guids);
  void addToSelection(const std::string& guid);          // the object becomes the primary
  // The selection without objects whose ancestor is also selected, in file order (what move/delete/duplicate act on).
  std::vector<std::string> topLevel(const std::vector<std::string>& guids) const;
  // Objects between `a` and `b` in hierarchy display order (depth first, file order), both included (shift+click).
  std::vector<std::string> displayRange(const std::string& a, const std::string& b) const;
  // Multi-selection (F3.01): selected() is the primary; extras are added with ctrl-click. select() clears them.
  // selection() lists the primary first.
  void toggleSelect(const std::string& guid);           // add to / remove from the selection (the primary stays valid)
  std::vector<std::string> selection() const;           // primary first, then the others in click order
  bool isSelected(const std::string& guid) const;

  // Edits. All return false (and change nothing) on bad input. Each pushes one undo step unless inside a gesture.
  bool setTransform(const std::string& guid, const float pos[3], const float rot[4], const float scale[3]);
  bool setWorldMatrix(const std::string& guid, const double world[16]);  // converts to local through the parent chain
  bool setActive(const std::string& guid, bool active);
  // Tag and layer are written only when they differ from the defaults ("" and 0), so old scene files stay byte-identical.
  bool setTag(const std::string& guid, const std::string& tag);
  bool setLayer(const std::string& guid, int layer);
  bool setField(const std::string& guid, int component, const std::string& field, const nlohmann::json& value);
  bool addComponent(const std::string& guid, const std::string& type, const nlohmann::json& fields = nlohmann::json::object());
  bool removeComponent(const std::string& guid, int component);
  bool moveComponent(const std::string& guid, int from, int to);          // reorders; to is the final index
  bool setComponentFields(const std::string& guid, int component, const nlohmann::json& fields);  // replaces the whole fields object
  std::string createObject(const std::string& name, const std::string& parentGuid,
                           const std::vector<SceneComponent>& components = {});  // returns the new GUID or ""
  std::string duplicate(const std::string& guid);  // object + children, new GUIDs, inner refs remapped; returns new root GUID
  bool remove(const std::string& guid);            // object + children
  bool rename(const std::string& guid, const std::string& name);
  bool reparent(const std::string& guid, const std::string& newParent);  // keeps the world transform
  // Batch edits: each is ONE undo step however many objects it touches. Descendants of selected objects follow their
  // ancestor and are not handled twice.
  // Moves the objects under `newParent` ("" = root), world transforms kept, placed before sibling `before` ("" = last).
  // This is also the sibling reorder: file order is sibling order. Refuses cycles and a `before` that is being moved.
  bool reparentMany(const std::vector<std::string>& guids, const std::string& newParent, const std::string& before = std::string());
  // Duplicates sit right after their originals; names get " (1)"; returns the new roots and selects them.
  std::vector<std::string> duplicateMany(const std::vector<std::string>& guids);
  bool removeMany(const std::vector<std::string>& guids);
  // Copy / paste of whole subtrees as JSON text (editor clipboard). Paste gives every object a new GUID, remaps inner
  // references, parents the roots under `parentGuid` and selects them.
  std::string copyObjects(const std::vector<std::string>& guids) const;
  std::vector<std::string> pasteObjects(const std::string& text, const std::string& parentGuid);

  // A gesture (a drag) is one undo step however many edits it contains.
  void beginGesture();
  void endGesture();

  bool canUndo() const { return stack_.canUndo(); }
  bool canRedo() const { return stack_.canRedo(); }
  size_t undoDepth() const { return stack_.depth(); }
  UndoStack& undoStack() { return stack_; }  // for other editing paths that record their own commands
  bool undo();
  bool redo();

  // F3.04 play mirror: while set, scene()/find()/childrenOf()/... read `mirror` (the live World's objects), edits go to
  // `sink`, and undo/redo and structural edits are refused. The saved document, selection and undo stack are untouched.
  void setPlayMirror(const Scene* mirror, PlayEditSink* sink) { mirror_ = mirror; sink_ = mirror ? sink : nullptr; }
  bool playing() const { return mirror_ != nullptr; }
  void selectRuntime(const std::string& guid) { selected_ = guid; extraSel_.clear(); }  // a runtime-only object

  // World transform helpers (column-major 4x4 of the object's TRS chain).
  void worldMatrix(const std::string& guid, double out[16]) const;

  // ---- Prefabs (F6.01, Editor 6) ----
  // The document stores prefab instance ROOTS (docs/rensga-engine/prefabs.md); scene() is the expanded view, so every object a
  // prefab brings is an ordinary SceneObject with a derived GUID. Edits on a member are recorded as overrides on its instance root,
  // through the same undo steps as every other edit. The library supplies the prefab files; without one instances stay unexpanded.
  void setPrefabLibrary(PrefabLibrary* lib);
  PrefabLibrary* prefabLibrary() const { return lib_; }
  // Rebuilds the view when a prefab file changed since the last build (no undo step, not dirty). True if it did.
  bool refreshPrefabs();
  const MemberInfo* memberInfo(const std::string& guid) const;        // nullptr for a plain scene object
  bool isInstanceRoot(const std::string& guid) const;                  // root of a prefab instance (stored in the scene)
  bool isPrefabMember(const std::string& guid) const;                  // brought by a prefab (not the root, not added to it)
  bool isAddedToInstance(const std::string& guid) const;               // object added under an instance (stored in its addedObjects)
  std::string instanceRootOf(const std::string& guid) const;           // root GUID of the instance holding it ("" = not part of one)
  std::string prefabOf(const std::string& guid) const;                 // asset GUID of that instance's prefab
  const std::vector<std::string>& prefabProblems() const { return prefabProblems_; }  // cycles, missing assets (shown in the Hierarchy)
  // Overrides. comp = component index in the object (-1: object-level property such as "name", "active", "tag", "layer").
  bool isOverridden(const std::string& guid, int comp, const std::string& field) const;
  bool hasObjectOverrides(const std::string& guid) const;              // anything recorded for this member (fields, added/removed components)
  int overrideCount(const std::string& rootGuid) const;                // every recorded modification of the instance
  nlohmann::json prefabValue(const std::string& guid, int comp, const std::string& field) const;  // the value without the instance's override (null if none)
  bool revertField(const std::string& guid, int comp, const std::string& field);
  bool applyField(const std::string& guid, int comp, const std::string& field);   // writes it into the instanced prefab (undo covers file and scene)
  bool revertObject(const std::string& guid);
  bool applyObject(const std::string& guid);
  bool revertInstance(const std::string& rootGuid);
  bool applyInstance(const std::string& rootGuid);
  // Makes a prefab asset from the object and its children (a new .prefab in `folder`, named after the object) and turns the object
  // into an instance of it, keeping its GUID. On an existing instance it makes a variant. Returns the prefab's asset GUID or "".
  std::string createPrefab(const std::string& guid, const std::filesystem::path& folder, std::string& err);
  // Adds an instance of the prefab under `parentGuid` ("" = scene root) and selects it. Returns the new root's GUID or "".
  std::string instantiatePrefab(const std::string& prefabGuid, const std::string& parentGuid, const float* position = nullptr);
  // Replaces the instance by plain objects (members keep their GUIDs, the link is dropped).
  bool unpackPrefab(const std::string& rootGuid);
  // Prefab edit mode: save() calls this instead of writing the scene file (the stage is not a scene on disk).
  void setSaveHook(std::function<bool(const nlohmann::ordered_json&, std::string&)> hook) { saveHook_ = std::move(hook); }
  bool hasSaveHook() const { return static_cast<bool>(saveHook_); }

 private:
  nlohmann::ordered_json* findJson(const std::string& guid);
  size_t jsonIndex(const std::string& guid) const;
  void collectSubtree(const std::string& guid, std::vector<std::string>& out) const;
  void pushUndo(const std::string& mergeKey = std::string());  // call before mutating doc_; changed() records the step
  friend class SnapshotCommand;
  void applySnapshot(const nlohmann::ordered_json& j);  // undo/redo target
  void changed();  // rebuild the view, mark dirty, fix the selection
  void rebuildView();  // doc_ -> view_ (expands prefab instances)
  // prefab helpers (prefab_scene.cpp)
  nlohmann::ordered_json* findInAdded(const std::string& guid);
  nlohmann::ordered_json* findInstanceJson(const std::string& rootGuid);
  bool setMemberField(const MemberInfo& m, const std::string& guid, int component, const std::string& field, const nlohmann::json& value);
  bool setMemberProp(const MemberInfo& m, const std::string& guid, const std::string& path, const nlohmann::json& value);
  bool addMemberComponent(const MemberInfo& m, const std::string& type, const nlohmann::json& fields);
  bool removeMemberComponent(const MemberInfo& m, int component);
  bool removeInstanceParts(const std::vector<std::string>& guids);   // members -> removedObjects, added objects -> erased
  std::string createAddedObject(const std::string& name, const MemberInfo& parent, const std::string& parentGuid, const std::vector<SceneComponent>& components);
  void fixMemberRemap(std::map<std::string, std::string>& remap) const;  // clones: members get GUIDs derived from the NEW root
  bool inInstance(const std::string& guid) const;  // a member or added object (not an instance root)
  const std::map<std::string, nlohmann::ordered_json>& baseObjects(const std::string& rootGuid) const;
  bool applyMods(const std::string& rootGuid, const PrefabModifications& part, const char* undoName);
  bool modsFor(const std::string& guid, int comp, const std::string& field, bool wholeObject, PrefabModifications& out) const;
  bool revertMods(const std::string& rootGuid, const PrefabModifications& part);
  bool usesPrefabs() const { return !members_.empty(); }
  PrefabLibrary* lib_ = nullptr;
  std::map<std::string, MemberInfo> members_;
  std::vector<std::string> prefabProblems_;
  int libRev_ = -1;
  mutable std::map<std::string, std::map<std::string, nlohmann::ordered_json>> baseCache_;
  std::function<bool(const nlohmann::ordered_json&, std::string&)> saveHook_;

  nlohmann::ordered_json doc_;
  UndoStack stack_;
  nlohmann::ordered_json before_;
  std::string beforeKey_;
  bool hasBefore_ = false;
  Scene view_;
  const Scene& live() const { return mirror_ ? *mirror_ : view_; }
  const Scene* mirror_ = nullptr;
  PlayEditSink* sink_ = nullptr;
  std::filesystem::path path_;
  std::string selected_;
  std::vector<std::string> extraSel_;
  bool loaded_ = false, forcedDirty_ = false;
};

}  // namespace editor
}  // namespace rensga
