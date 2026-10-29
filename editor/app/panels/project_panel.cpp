// Project window: read-only tree of Assets/.
#include <algorithm>

#include "app.h"

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

static void drawNode(const AssetNode& n) {
  if (n.dir) {
    if (ImGui::TreeNodeEx(n.name.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen)) {
      for (const AssetNode& c : n.children) drawNode(c);
      ImGui::TreePop();
    }
  } else {
    ImGui::TreeNodeEx(n.name.c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                          ImGuiTreeNodeFlags_SpanFullWidth);
  }
}

void drawProjectPanel(App& a) {
  if (!a.showProject) return;
  if (!ImGui::Begin("Project", nullptr)) { ImGui::End(); return; }
  if (!a.loaded) {
    ImGui::TextDisabled("No project loaded");
  } else {
    if (ImGui::GetTime() - a.assetsStamp > 2.0) { a.refreshAssets(); a.assetsStamp = ImGui::GetTime(); }
    drawNode(a.assets);
  }
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
