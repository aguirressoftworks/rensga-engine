// MVP-47: the editable scene. The scene JSON (ordered, so saves keep key order and diffs small) is the
// source of truth; scene() is a typed view rebuilt after every change. Undo/redo are JSON snapshots.
// No ImGui, no console names.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/editor/project.h"

namespace rensga {
namespace editor {

std::string generateGuid();  // random (v4) GUID, lowercase 8-4-4-4-12

class SceneDocument {
 public:
  static constexpr size_t kMaxUndo = 200;

  bool load(const std::filesystem::path& file, std::string& err);
  bool save(std::string& err);  // writes to path()
  bool loaded() const { return loaded_; }
  const std::filesystem::path& path() const { return path_; }
  bool dirty() const { return dirty_; }
  const Scene& scene() const { return view_; }
  const nlohmann::ordered_json& document() const { return doc_; }
  // Replaces the document with `j` as if loaded from `file` (editor restart): undo history is dropped, dirty is kept.
  bool restore(const std::filesystem::path& file, nlohmann::ordered_json j, bool dirty);
  const SceneObject* find(const std::string& guid) const;
  int indexOf(const std::string& guid) const;  // index in scene().objects or -1
  std::vector<std::string> childrenOf(const std::string& parentGuid) const;  // "" = roots, in file order
  bool isDescendant(const std::string& guid, const std::string& ancestor) const;

  // Selection (by GUID). Not part of undo; cleared when the object disappears.
  const std::string& selected() const { return selected_; }
  void select(const std::string& guid);

  // Edits. All return false (and change nothing) on bad input. Each pushes one undo step unless inside a gesture.
  bool setTransform(const std::string& guid, const float pos[3], const float rot[4], const float scale[3]);
  bool setWorldMatrix(const std::string& guid, const double world[16]);  // converts to local through the parent chain
  bool setActive(const std::string& guid, bool active);
  bool setField(const std::string& guid, int component, const std::string& field, const nlohmann::json& value);
  bool addComponent(const std::string& guid, const std::string& type, const nlohmann::json& fields = nlohmann::json::object());
  bool removeComponent(const std::string& guid, int component);
  std::string createObject(const std::string& name, const std::string& parentGuid,
                           const std::vector<SceneComponent>& components = {});  // returns the new GUID or ""
  std::string duplicate(const std::string& guid);  // object + children, new GUIDs, inner refs remapped; returns new root GUID
  bool remove(const std::string& guid);            // object + children
  bool rename(const std::string& guid, const std::string& name);
  bool reparent(const std::string& guid, const std::string& newParent);  // keeps the world transform

  // A gesture (a drag) is one undo step however many edits it contains.
  void beginGesture();
  void endGesture();

  bool canUndo() const { return !undo_.empty(); }
  bool canRedo() const { return !redo_.empty(); }
  size_t undoDepth() const { return undo_.size(); }
  bool undo();
  bool redo();

  // World transform helpers (column-major 4x4 of the object's TRS chain).
  void worldMatrix(const std::string& guid, double out[16]) const;

 private:
  nlohmann::ordered_json* findJson(const std::string& guid);
  size_t jsonIndex(const std::string& guid) const;
  void collectSubtree(const std::string& guid, std::vector<std::string>& out) const;
  void pushUndo();
  void changed();  // rebuild the view, mark dirty, fix the selection

  nlohmann::ordered_json doc_;
  std::vector<nlohmann::ordered_json> undo_, redo_;
  Scene view_;
  std::filesystem::path path_;
  std::string selected_;
  bool loaded_ = false, dirty_ = false, inGesture_ = false, gesturePushed_ = false;
};

}  // namespace editor
}  // namespace rensga
