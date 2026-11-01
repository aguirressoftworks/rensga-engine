// F6.07: the Inspector of a data asset (*.asset.json) and Project > Create > Data Asset > <Type>.
#pragma once
#include <string>

#include "app.h"
#include "rensga/editor/asset_database.h"

namespace rensga {
namespace editor {

// Draws the data asset in the Inspector (called by drawAssetInspector). Returns true: the asset is fully handled here.
bool drawDataAssetInspector(App& a, const AssetEntry& e);
// Create > Data Asset > <Type> in the Project window's current folder; selects the new asset. False (and a log line) on failure.
bool createDataAssetFromMenu(App& a, const std::string& typeName, const std::string& name);
// The "Data Asset" submenu of the Create menu (one item per data asset type, or a disabled hint when the scripts define none).
void drawCreateDataAssetMenu(App& a);

}  // namespace editor
}  // namespace rensga
