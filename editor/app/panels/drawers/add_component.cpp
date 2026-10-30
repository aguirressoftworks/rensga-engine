#include <algorithm>
#include <map>

#include "drawers.h"
#include "rensga/editor/inspector_model.h"

namespace rensga {
namespace editor {

namespace {

struct Entry {
  std::string name, category, reason;  // reason: why it is greyed ("" = can be added)
  int score = 0;
  bool script = false;
};

void addTo(App& a, const std::vector<std::string>& guids, const Entry& e) {
  a.doc.beginGesture();
  for (const std::string& g : guids) {
    if (e.script) {  // a project script: the ConsoleTarget list learns it first (the cook only accepts listed components)
      addScriptToObject(a, g, e.name);
    } else {
      OpResult r = addComponentTo(a.doc, g, e.name);
      if (!r) a.status = r.reason;
    }
  }
  a.doc.endGesture();
  if (e.script) noteComponentUsed(e.name);
}

}  // namespace

void drawAddComponentPopup(App& a, const char* popupId, const std::vector<std::string>& guids, const std::set<std::string>& allowed,
                           const std::string& targetName) {
  ImGui::SetNextWindowSizeConstraints(ImVec2(280, 120), ImVec2(FLT_MAX, 420));
  if (!ImGui::BeginPopup(popupId)) return;
  static char search[96] = {};
  if (ImGui::IsWindowAppearing()) { search[0] = 0; ImGui::SetKeyboardFocusHere(); }
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##q", "Search components", search, sizeof search);
  ImGui::Separator();

  const SceneObject* first = guids.empty() ? nullptr : a.doc.find(guids[0]);
  std::vector<Entry> all;
  std::set<std::string> seen, projectScripts;
  for (const ScriptInfo& sc : a.scripts.list) projectScripts.insert(sc.name);
  const TypeRegistry& reg = TypeRegistry::instance();
  for (u32 i = 0; i < reg.count(); ++i) {
    const TypeInfo* ti = reg.byId(i);
    if (!ti || !ti->asComponent) continue;
    Entry e;
    e.name = ti->name;
    e.category = componentCategory(*ti);
    e.script = projectScripts.count(e.name) > 0;
    // a script is added through the script path, which also lists it in the ConsoleTarget
    std::set<std::string> rules = e.script ? std::set<std::string>() : allowed;
    if (first) e.reason = addBlockReason(e.name, *first, rules, targetName);
    all.push_back(e);
    seen.insert(e.name);
  }
  for (const ScriptInfo& sc : a.scripts.list) {  // on disk but not compiled yet
    if (seen.count(sc.name)) continue;
    Entry e;
    e.name = sc.name;
    e.category = "Scripts";
    e.script = true;
    e.reason = scriptStatus(a, sc.name);
    if (e.reason.empty()) e.reason = "needs rebuild";
    all.push_back(e);
  }

  auto item = [&](const Entry& e, const char* suffix) {
    const bool off = !e.reason.empty();
    ImGui::BeginDisabled(off);
    bool hit = ImGui::Selectable((e.name + "##" + suffix).c_str());
    ImGui::EndDisabled();
    if (off && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", e.reason.c_str());
    if (hit) { addTo(a, guids, e); ImGui::CloseCurrentPopup(); }
  };

  ImGui::BeginChild("list", ImVec2(0, 300));
  if (search[0]) {
    std::vector<Entry> hits;
    for (Entry e : all) {
      e.score = fuzzyScore(search, e.name);
      if (e.score >= 0) hits.push_back(e);
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Entry& x, const Entry& y) { return x.score > y.score; });
    if (hits.empty()) ImGui::TextDisabled("No component matches \"%s\"", search);
    for (const Entry& e : hits) {
      item(e, "s");
      ImGui::SameLine();
      ImGui::TextDisabled("  %s", e.category.c_str());
    }
  } else {
    std::vector<Entry> recent;
    for (const std::string& n : recentComponents())
      for (const Entry& e : all)
        if (e.name == n) recent.push_back(e);
    if (!recent.empty()) {
      ImGui::SeparatorText("Recently used");
      for (const Entry& e : recent) item(e, "r");
    }
    std::map<std::string, std::vector<const Entry*>> byCat;
    for (const Entry& e : all) byCat[e.category].push_back(&e);
    for (auto& kv : byCat) {
      std::sort(kv.second.begin(), kv.second.end(), [](const Entry* x, const Entry* y) { return x->name < y->name; });
      if (ImGui::TreeNodeEx(kv.first.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const Entry* e : kv.second) item(*e, "c");
        ImGui::TreePop();
      }
    }
  }
  ImGui::EndChild();
  ImGui::EndPopup();
}

}  // namespace editor
}  // namespace rensga
