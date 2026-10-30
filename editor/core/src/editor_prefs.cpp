#include "rensga/editor/editor_prefs.h"

#include <fstream>
#include <mutex>

#include <nlohmann/json.hpp>

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

namespace {
std::mutex g_mu;
fs::path g_dir;
nlohmann::json g_prefs = nlohmann::json::object();

void loadLocked() {
  g_prefs = nlohmann::json::object();
  if (g_dir.empty()) return;
  std::ifstream in(g_dir / "editor_prefs.json", std::ios::binary);
  if (!in) return;
  nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
  if (j.is_object()) g_prefs = j;
}
}  // namespace

void setPrefsDir(const fs::path& dir) {
  std::lock_guard<std::mutex> g(g_mu);
  g_dir = dir;
  loadLocked();
}

fs::path prefsFile() {
  std::lock_guard<std::mutex> g(g_mu);
  return g_dir.empty() ? fs::path() : g_dir / "editor_prefs.json";
}

std::string externalEditorPref() {
  std::lock_guard<std::mutex> g(g_mu);
  return g_prefs.value("externalEditor", std::string());
}

void setExternalEditorPref(const std::string& tmpl) {
  std::lock_guard<std::mutex> g(g_mu);
  if (tmpl.empty()) g_prefs.erase("externalEditor");
  else g_prefs["externalEditor"] = tmpl;
  if (g_dir.empty()) return;
  std::error_code ec;
  fs::create_directories(g_dir, ec);
  std::ofstream o(g_dir / "editor_prefs.json", std::ios::binary | std::ios::trunc);
  o << g_prefs.dump(1) << "\n";
}

}  // namespace editor
}  // namespace rensga
