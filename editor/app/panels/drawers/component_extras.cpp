// F4.08 part 2: widgets drawn under a component's reflected fields in the Inspector. SpriteRenderer gets a sprite picker
// (the named slices of its texture's .meta, with thumbnails); Tilemap gets "Open Tile Palette" / "New Tilemap Asset...".
#include <algorithm>
#include <cfloat>

#include "panels/asset_ui.h"
#include "panels/drawers/component_inspector.h"
#include "panels/drawers/drawers.h"
#include "panels/tile_tools.h"

namespace rensga {
namespace editor {

bool& spritePickerOpenRequest() {
  static bool r = false;
  return r;
}

namespace {

using nlohmann::json;

std::string stringField(App& a, const ComponentRef& r, const char* field) {
  const json v = fieldValue(a.doc, r, field);
  return v.is_string() ? v.get<std::string>() : std::string();
}

int intField(App& a, const ComponentRef& r, const char* field) {
  const json v = fieldValue(a.doc, r, field);
  return v.is_number() ? v.get<int>() : 0;
}

bool beginRow(const char* id, const char* label) {
  if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_SizingStretchProp)) return false;
  ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthStretch, 0.40f);
  ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch, 0.60f);
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::TableSetColumnIndex(1);
  return true;
}

void setWholeTexture(App& a, const std::vector<ComponentRef>& refs) {
  a.doc.beginGesture();
  for (const char* f : {"srcX", "srcY", "srcW", "srcH", "pivotX", "pivotY"}) editFields(a.doc, refs, f, 0);
  a.doc.endGesture();
}

void spritePicker(App& a, const std::vector<ComponentRef>& refs, bool multi) {
  bindScene2dResources(a);
  const ComponentRef& r = refs[0];
  const std::string texGuid = stringField(a, r, "texture");
  if (!beginRow("##sprite", "Sprite")) return;
  const bool hasTex = !texGuid.empty() && texGuid != "00000000-0000-0000-0000-000000000000" && assetUi().sync.db().findGuid(texGuid) != nullptr;
  SpriteSlices slices;
  std::string err;
  if (hasTex) loadSlices(assetUi().sync.db(), texGuid, slices, err);
  if (!hasTex) {
    ImGui::TextDisabled("Set a texture first");
  } else {
    const int sw = intField(a, r, "srcW"), sh = intField(a, r, "srcH");
    int cur = sw == 0 ? -2 : slices.indexOfSrc(intField(a, r, "srcX"), intField(a, r, "srcY"), sw, sh);
    std::string label = multi ? std::string(mixedText()) : cur == -2 ? "(whole texture)" : cur >= 0 ? slices.rects[static_cast<size_t>(cur)].name : "(custom rectangle)";
    if (slices.empty()) label = "No slices: open the Sprite Editor";
    const float editW = ImGui::CalcTextSize("Edit").x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::BeginDisabled(slices.empty());
    if (ImGui::Button((label + "##spick").c_str(), ImVec2(ImGui::GetContentRegionAvail().x - editW - 6, 0))) ImGui::OpenPopup("sprite_pick");
    ImGui::EndDisabled();
    if (spritePickerOpenRequest() && !slices.empty()) { spritePickerOpenRequest() = false; ImGui::OpenPopup("sprite_pick"); }
    ImGui::SameLine();
    if (ImGui::Button("Edit##spedit")) openSpriteEditor(a, texGuid);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Open this texture in the Sprite Editor");
    ImGui::SetNextWindowSizeConstraints(ImVec2(260, 0), ImVec2(420, 360));
    if (ImGui::BeginPopup("sprite_pick")) {
      const Scene2DResources::Tex* tex = a.res2d.texture(texGuid);
      ImDrawList* dl = ImGui::GetWindowDrawList();
      if (ImGui::Selectable("(whole texture)", cur == -2)) setWholeTexture(a, refs);
      for (size_t i = 0; i < slices.rects.size(); ++i) {
        const SpriteRect& sr = slices.rects[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable("##pick", static_cast<int>(i) == cur, 0, ImVec2(0, 36))) applySliceByName(a, refs, sr.name);
        const ImVec2 mn = ImGui::GetItemRectMin();
        if (tex && tex->w > 0 && tex->h > 0) {
          const float k = std::min(28.0f / static_cast<float>(std::max(1, sr.w)), 28.0f / static_cast<float>(std::max(1, sr.h)));
          const ImVec2 a0(mn.x + 4, mn.y + 4), a1(a0.x + static_cast<float>(sr.w) * k, a0.y + static_cast<float>(sr.h) * k);
          dl->AddRectFilled(ImVec2(mn.x + 3, mn.y + 3), ImVec2(mn.x + 33, mn.y + 33), ImGui::GetColorU32(brandToken("surface-2")));
          imageNearestBegin(dl);
          dl->AddImage(static_cast<ImTextureID>(tex->gl), a0, a1, ImVec2(static_cast<float>(sr.x) / static_cast<float>(tex->w), static_cast<float>(sr.y) / static_cast<float>(tex->h)),
                       ImVec2(static_cast<float>(sr.x + sr.w) / static_cast<float>(tex->w), static_cast<float>(sr.y + sr.h) / static_cast<float>(tex->h)));
          imageNearestEnd(dl);
        }
        dl->AddText(ImVec2(mn.x + 42, mn.y + 3), ImGui::GetColorU32(brandToken("text")), sr.name.c_str());
        char info[64];
        std::snprintf(info, sizeof info, "%d,%d  %dx%d", sr.x, sr.y, sr.w, sr.h);
        dl->AddText(ImVec2(mn.x + 42, mn.y + 19), ImGui::GetColorU32(brandToken("text-2")), info);
        ImGui::PopID();
      }
      ImGui::EndPopup();
    }
  }
  ImGui::EndTable();
}

void tilemapButtons(App& a, const std::vector<ComponentRef>& refs) {
  const std::string map = stringField(a, refs[0], "map");
  const bool none = map.empty() || map == "00000000-0000-0000-0000-000000000000" || !assetUi().sync.db().findGuid(map);
  ImGui::Spacing();
  if (!none) {
    if (ImGui::Button("Open Tile Palette", ImVec2(-FLT_MIN, 0))) openTilePalette(a, map);
  } else {
    if (ImGui::Button("New Tilemap Asset...", ImVec2(-FLT_MIN, 0))) openTilePalette(a, std::string(), true);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Creates a *.tilemap.json from a tileset texture and assigns it here");
  }
}

}  // namespace

// Registered through the component inspector registry (component_inspector.h); the old drawComponentExtras if-chain is gone.
static void tilemapInspector(App& a, const std::vector<ComponentRef>& refs, bool multi) {
  if (!multi) tilemapButtons(a, refs);
}
RENSGA_COMPONENT_INSPECTOR(sprite_renderer, "SpriteRenderer", spritePicker)
RENSGA_COMPONENT_INSPECTOR(tilemap, "Tilemap", tilemapInspector)

}  // namespace editor
}  // namespace rensga
