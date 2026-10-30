// Editor preferences kept in the editor's config dir (<config>/editor_prefs.json). No ImGui.
#pragma once
#include <filesystem>
#include <string>

namespace rensga {
namespace editor {

constexpr const char* kDefaultExternalEditor = "code -g {file}:{line}:{col}";

// Where the preferences live. Call once at start with the config dir (RENSGA_HOME/.config/rensga); loads the file.
void setPrefsDir(const std::filesystem::path& dir);
std::filesystem::path prefsFile();
// The external code editor command template ("" = use the default). Saved on set.
std::string externalEditorPref();
void setExternalEditorPref(const std::string& tmpl);

}  // namespace editor
}  // namespace rensga
