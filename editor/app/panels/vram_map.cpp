// VRAM Map panel (F3.02, Window > VRAM Map): what the project's target keeps in video memory. Items come from data
// (ConsoleTarget size, resolution, framebuffer format, cooked texture sizes); the TargetModule places them when there is
// one, else the generic linear layout does. Over budget is drawn with the danger token and the words "Over budget".
#include <algorithm>
#include <cstdio>

#include "app.h"
#include "panels/asset_ui.h"

namespace rensga {
namespace editor {

static std::string sizeText(uint64_t b) {
  char t[48];
  if (b >= 1024 * 1024) std::snprintf(t, sizeof t, "%.2f MB", b / 1048576.0);
  else if (b >= 1024) std::snprintf(t, sizeof t, "%.1f KB", b / 1024.0);
  else std::snprintf(t, sizeof t, "%llu B", static_cast<unsigned long long>(b));
  return t;
}

// Tokens only (no raw palette steps): framebuffers info, depth text-2, textures link, palettes primary.
static ImVec4 kindColor(const std::string& kind) {
  if (kind == "framebuffer") return brandToken("info");
  if (kind == "depth") return brandToken("text-2");
  if (kind == "palette") return brandToken("primary");
  return brandToken("link");
}

void drawVramMapPanel(App& a) {
  AssetUi& u = assetUi();
  if (!u.showVram) return;
  ImGui::SetNextWindowSize(ImVec2(640, 560), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("VRAM Map", &u.showVram)) { ImGui::End(); return; }
  if (!a.loaded) { ImGui::TextDisabled("No project loaded"); ImGui::End(); return; }
  const ConsoleTargetData& ct = u.ct;
  const size_t count = u.sync.db().entries().size();
  if (u.vramKeyGen != u.sync.generation() || u.vramKeyCount != count || u.vramKeyCap != ct.vramBytes) {
    u.vramKeyGen = u.sync.generation();
    u.vramKeyCount = count;
    u.vramKeyCap = ct.vramBytes;
    u.vramItems = collectVramItems(ct, u.sync);
    u.vramKeyGen = u.sync.generation();  // collecting imports textures, which bumps the generation
    u.vramLayout = layoutVram(a.targetModule(), ct, u.vramItems);
  }
  const VramLayout& L = u.vramLayout;
  ImGui::Text("%s  -  %ux%u, %s video memory", ct.profile.c_str(), ct.width, ct.height, sizeText(L.capacity).c_str());
  if (!a.targetModule()) ImGui::TextDisabled("No editor module for this target yet: generic linear layout.");
  const bool over = L.overBudget();
  if (over) {
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
    ImGui::Text("[!] Over budget: %s used of %s (%s too much)", sizeText(L.used).c_str(), sizeText(L.capacity).c_str(), sizeText(L.used - L.capacity).c_str());
    ImGui::PopStyleColor();
  } else {
    ImGui::TextColored(brandToken("ok"), "OK");
    ImGui::SameLine();
    ImGui::Text("%s used of %s (%.1f%%)", sizeText(L.used).c_str(), sizeText(L.capacity).c_str(), L.capacity ? 100.0 * L.used / L.capacity : 0.0);
  }

  // The memory as rows of equal length, address 0 at the top left. An item can span several rows.
  const int rows = 12;
  const float W = std::max(120.0f, ImGui::GetContentRegionAvail().x - 4), rowH = 14, gap = 2;
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const uint64_t cap = std::max<uint64_t>(L.capacity, 1), perRow = (cap + rows - 1) / rows;
  for (int r = 0; r < rows; ++r) {
    const float y = p0.y + r * (rowH + gap);
    dl->AddRectFilled(ImVec2(p0.x, y), ImVec2(p0.x + W, y + rowH), ImGui::GetColorU32(brandToken("surface-2")));
  }
  const VramPlacement* hover = nullptr;
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  for (const VramPlacement& p : L.items) {
    const uint64_t b0 = std::min(p.offset, cap), b1 = std::min(p.offset + p.size, cap);
    for (uint64_t at = b0; at < b1;) {
      const int r = static_cast<int>(at / perRow);
      const uint64_t rowEnd = std::min<uint64_t>(b1, static_cast<uint64_t>(r + 1) * perRow);
      const float x0 = p0.x + W * static_cast<float>(at - r * perRow) / perRow, x1 = p0.x + W * static_cast<float>(rowEnd - r * perRow) / perRow;
      const float y = p0.y + r * (rowH + gap);
      ImVec4 c = kindColor(p.item.kind);
      dl->AddRectFilled(ImVec2(x0, y), ImVec2(std::max(x1, x0 + 1), y + rowH), ImGui::GetColorU32(c));
      if (mouse.x >= x0 && mouse.x <= x1 && mouse.y >= y && mouse.y <= y + rowH) hover = &p;
      at = rowEnd;
    }
  }
  ImGui::Dummy(ImVec2(W, rows * (rowH + gap)));
  if (over) {
    const float frac = std::min(1.0f, static_cast<float>(L.used - L.capacity) / cap);
    const ImVec2 q0 = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(q0, ImVec2(q0.x + std::max(4.0f, W * frac), q0.y + rowH), ImGui::GetColorU32(brandToken("danger")));
    ImGui::Dummy(ImVec2(W, rowH));
    ImGui::TextColored(brandToken("danger"), "[!] Over budget: the red bar is the part that does not fit");
  }
  if (hover) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(hover->item.name.c_str());
    ImGui::TextDisabled("%s  offset %s  size %s%s", hover->item.kind.c_str(), sizeText(hover->offset).c_str(), sizeText(hover->size).c_str(),
                        hover->outside ? "  (over budget)" : "");
    if (hover->item.width) ImGui::TextDisabled("%ux%u, %u-bit", hover->item.width, hover->item.height, hover->item.bits);
    ImGui::EndTooltip();
  }
  // legend
  const char* kinds[] = {"framebuffer", "depth", "texture", "palette"};
  for (int i = 0; i < 4; ++i) {
    if (i) ImGui::SameLine();
    const ImVec2 c = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(ImVec2(c.x, c.y + 3), ImVec2(c.x + 10, c.y + 13), ImGui::GetColorU32(kindColor(kinds[i])));
    ImGui::Dummy(ImVec2(14, 0));
    ImGui::SameLine();
    ImGui::TextUnformatted(kinds[i]);
  }
  ImGui::Separator();
  if (ImGui::BeginTable("##vram", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Item");
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 90);
    ImGui::TableSetupColumn("Offset", ImGuiTableColumnFlags_WidthFixed, 80);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 80);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    for (const VramPlacement& p : L.items) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      const bool bad = p.outside;
      if (bad) ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
      ImGui::PushID(&p);
      if (ImGui::Selectable(((bad ? "[!] " : "") + p.item.name).c_str(), p.item.guid == u.selGuid && !p.item.guid.empty(), ImGuiSelectableFlags_SpanAllColumns) &&
          !p.item.guid.empty())
        selectAsset(a, p.item.guid);
      ImGui::PopID();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(p.item.kind.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(sizeText(p.offset).c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(sizeText(p.size).c_str());
      if (bad) { ImGui::SameLine(); ImGui::TextUnformatted("Over budget"); ImGui::PopStyleColor(); }
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
