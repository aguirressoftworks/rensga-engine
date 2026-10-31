// Creation stubs for the F6 asset kinds (Project window: Create > Prefab / Material / Animator Controller / Property Clip / Data Asset).
// A stub is the smallest valid file of the kind. The animator controller, property clip and data asset schemas are filled in
// by their own phases (Editor 8, 9, 10); until then the stub is {"format":1,"kind":"<kind>"} so the file imports and shows up.
#pragma once
#include <filesystem>
#include <string>

#include "rensga/editor/asset_database.h"

namespace rensga {
namespace editor {

struct StubKind {
  AssetKind kind;
  const char* label;      // menu text: "Prefab", "Material", ...
  const char* extension;  // ".prefab", ".mat.json", ...
  const char* baseName;   // "New Prefab"
  const char* glyph;      // 3-letter Project window tag: "PFB", "MAT", ...
};
const StubKind* stubKinds(int* count);                // the five F6 kinds in menu order
const StubKind* stubKindFor(AssetKind k);             // nullptr for kinds that are not created through a stub

// The stub's file text. For a prefab `guid` is its own asset GUID (empty at creation: fixed up by createAssetStub).
std::string stubText(AssetKind k, const std::string& displayName, const std::string& guid);

// Writes <folder>/<baseName> N.ext (first free name when `name` is empty or taken), runs db.scan() so it gets its .meta
// and, for a prefab, rewrites the "guid" key with the GUID the .meta carries. Returns the created path (absolute).
bool createAssetStub(AssetDatabase& db, AssetKind k, const std::filesystem::path& folder, const std::string& name,
                     std::filesystem::path& created, std::string& err);

}  // namespace editor
}  // namespace rensga
