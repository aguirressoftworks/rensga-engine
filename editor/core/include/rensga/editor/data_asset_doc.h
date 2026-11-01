// F6.07 (Editor 13): the document behind the Inspector of a data asset (*.asset.json, the ScriptableObject equivalent).
//
//   {"format": 1, "kind": "DataAsset", "type": "EnemyStats", "fields": {"health": 30, "speed": 3.0, ...}}
//
// The type is a reflected, non-component struct of the project scripts. Edits validate against the reflected field, are undoable (one
// step per edit, or per gesture such as a slider drag) and are written to the file at once (atomic temp file + rename). No ImGui.
#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/core/reflect.h"
#include "rensga/editor/asset_database.h"

namespace rensga {
namespace editor {

// Names of the types a data asset can have (reflected, not a Component or Behaviour), sorted.
std::vector<std::string> dataAssetTypeNames();

// Creates <folder>/<name or "New <Type>">.asset.json holding the type's defaults (first free name), imports it and returns the path.
bool createDataAsset(AssetDatabase& db, const std::filesystem::path& folder, const std::string& typeName, const std::string& name,
                     std::filesystem::path& created, std::string& err);

class DataAssetDoc {
 public:
  bool load(const std::filesystem::path& file, std::string& err);
  const std::filesystem::path& path() const { return path_; }
  const std::string& typeName() const { return type_; }
  // The reflection of the type; null while the script that defines it is not loaded (the file is kept as it is).
  const TypeInfo* type() const;
  // Effective values (stored, else the default) of every field of the type, in declaration order.
  nlohmann::json values() const;
  nlohmann::json value(const std::string& field) const;
  // Stored fields the type no longer has (a renamed or removed script field). The cooker refuses them; the next edit drops them.
  std::vector<std::string> unknownFields() const;

  // False with `err` when the field is unknown or the value does not fit it. A valid edit changes the file at once.
  bool set(const std::string& field, const nlohmann::json& value, std::string& err);
  void beginGesture();  // edits until endGesture are one undo step
  void endGesture();
  bool undo();
  bool redo();
  size_t undoDepth() const { return undo_.size(); }
  size_t redoDepth() const { return redo_.size(); }
  bool save(std::string& err);  // done by every set/undo/redo; public for tests

 private:
  void push();
  std::filesystem::path path_;
  nlohmann::ordered_json raw_ = nlohmann::ordered_json::object();
  std::string type_;
  std::vector<nlohmann::ordered_json> undo_, redo_;  // snapshots of "fields"
  int gesture_ = 0;
  bool gesturePushed_ = false;
};

}  // namespace editor
}  // namespace rensga
