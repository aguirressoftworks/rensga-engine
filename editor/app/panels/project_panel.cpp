// Project window: tree of Assets/. Scripts are listed by class name, drag onto an object, double-click to open.
#include <algorithm>
#include <set>

#include "app.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

static void scan(const fs::path& dir, AssetNode& node, int depth) {
  std::error_code ec;
  std::vector<fs::directory_entry> es;
  for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) es.push_back(*it);
  std::sort(es.begin(), es.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
    bool da = a.is_directory(), db = b.is_directory();
    return da != db ? da : a.path().filename() < b.path().filename();
  });
  for (const auto& e : es) {
    std::string n = e.path().filename().string();
    if (e.path().extension() == ".meta") continue;  // sidecars are noise here
    AssetNode c;
    c.name = n;
    c.path = e.path();
    c.dir = e.is_directory();
    if (c.dir && depth < 8) scan(e.path(), c, depth + 1);
    node.children.push_back(std::move(c));
  }
}

void App::refreshAssets() {
  assets = AssetNode();
  assets.name = "Assets";
  assets.dir = true;
  if (loaded) scan(project.root / "Assets", assets, 0);
}

static const ScriptInfo* scriptAt(const App& a, const fs::path& p) {
  for (const ScriptInfo& sc : a.scripts.list)
    if (sc.path == p) return &sc;
  return nullptr;
}

static void drawNode(App& a, const AssetNode& n) {
  if (n.dir) {
    if (ImGui::TreeNodeEx(n.name.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
      for (const AssetNode& c : n.children) drawNode(a, c);
      ImGui::TreePop();
    }
    return;
  }
  const ScriptInfo* sc = scriptAt(a, n.path);
  const bool code = n.path.extension() == ".cpp" || n.path.extension() == ".h";
  // a script shows by class name (file name = class name), like Unity
  const std::string label = sc ? n.path.stem().string() : n.name;
  const std::string why = sc ? scriptStatus(a, sc->name) : std::string();
  if (!why.empty()) ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
  ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
  if (!why.empty()) ImGui::PopStyleColor();
  const bool dbl = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  if (sc && ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload("RENSGA_SCRIPT", sc->name.c_str(), sc->name.size() + 1);
    ImGui::Text("%s", sc->name.c_str());
    ImGui::EndDragDropSource();
  }
  if (sc) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", why.empty() ? "script" : why.c_str());
  }
  if (code && dbl && !openInExternalEditor(n.path))
    EditorLog::instance().warn("cannot open " + n.path.string() + " with " + externalEditorCommand());
}

void drawProjectPanel(App& a) {
  if (!a.showProject) return;
  if (!ImGui::Begin("Project", nullptr)) { ImGui::End(); return; }
  if (!a.loaded) {
    ImGui::TextDisabled("No project loaded");
  } else {
    if (ImGui::GetTime() - a.assetsStamp > 2.0) { a.refreshAssets(); a.assetsStamp = ImGui::GetTime(); }
    drawNode(a, a.assets);
    if (ImGui::BeginPopupContextWindow("proj_ctx", ImGuiPopupFlags_MouseButtonRight)) {
      if (ImGui::BeginMenu("Create")) {
        if (ImGui::MenuItem("C++ Script")) {
          // first free "NewScript", "NewScript1", ...
          std::string base = "NewScript", name = base;
          std::set<std::string> none;
          for (int i = 1; !validateScriptName(a.project.root, name, none).empty() && i < 100; ++i) name = base + std::to_string(i);
          std::snprintf(a.scripts.newName, sizeof a.scripts.newName, "%s", name.c_str());
          a.scripts.openNewScript = true;
        }
        ImGui::EndMenu();
      }
      ImGui::EndPopup();
    }
  }
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
