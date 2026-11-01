// F6.02: the Material Editor window (material_editor.cpp).
#pragma once
#include <string>

namespace rensga {
namespace editor {
struct App;
// Opens the window on a .mat.json asset, by project-relative path or GUID. Logs a warning when it is no material.
void openMaterialEditor(App& a, const std::string& pathOrGuid);
}  // namespace editor
}  // namespace rensga
