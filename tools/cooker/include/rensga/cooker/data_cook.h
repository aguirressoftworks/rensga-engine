// F6.07 (Editor 13): data assets (*.asset.json) -> the DATA section. The file format is documented in
// docs/rensga-engine/editor-extensions.md ("Data assets"):
//
//   {"format": 1, "kind": "DataAsset", "type": "EnemyStats", "fields": {"health": 30, "speed": 3.0, "label": "Grunt"}}
//
// "type" names a reflected, non-component type of the project scripts; "fields" holds the values that differ from the defaults (a
// missing field takes the default, an unknown field is an error naming the asset). The cooker and the editor bake with the same
// function, so what the Inspector shows is what the pack holds.
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/core/pack_format.h"
#include "rensga/core/reflect.h"

namespace rensga {
namespace cooker {

struct DataIssue {
  std::string message, asset, guid;
};

struct DataAssetRef {
  std::filesystem::path path;  // absolute
  std::string rel;             // project-relative, '/' separators
  std::string guid;            // from the .meta
};

// Reads {"type": ..., "fields": ...} from a parsed *.asset.json. False with `err` when the file has no type or the type is not a
// data asset type (unknown, or a Component / Behaviour).
bool parseDataAsset(const nlohmann::json& doc, const TypeInfo*& type, nlohmann::json& fields, std::string& err);

// True when `t` can be a data asset: reflected, not a Component or Behaviour, at most kDataAssetMaxBytes of blob.
bool isDataAssetType(const TypeInfo& t);

// Every registered data asset type, by name.
std::vector<const TypeInfo*> dataAssetTypes();

// Cooks every asset (sorted by path, so the section is deterministic): the field blob (trailing appended fields that are still
// default are left out, like components) goes to `blob` 16-byte aligned. `addStr` stores the asset name. Problems are appended to
// `issues`; an asset with a problem is skipped.
void cookDataAssets(const std::vector<DataAssetRef>& assets, const std::function<u32(const std::string&)>& addStr, std::vector<u8>& blob,
                    std::vector<PackDataAsset>& out, std::vector<DataIssue>& issues);

}  // namespace cooker
}  // namespace rensga
