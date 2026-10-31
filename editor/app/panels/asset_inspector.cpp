// Asset inspector (F3.02): import settings of the selected Project asset. A tab strip picks Default or one build target
// (the project's console target first); a target tab holds overrides laid over the defaults. Apply writes the .meta and
// reimports through the cooker's cache; Revert drops the unsaved edits; Undo/Redo step through applied changes.
// The preview cooks the unsaved settings for the tab's target (no cache) so the effect shows before Apply.
#include <algorithm>
#include <cstdlib>

#include "app.h"
#include "panels/asset_ui.h"
#include "panels/tile_tools.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/inspector_model.h"

namespace rensga {
namespace editor {

using nlohmann::json;
namespace fs = std::filesystem;

static const AssetEntry* selected(AssetUi& u) { return u.sync.db().findGuid(u.selGuid); }

static void loadPending(AssetUi& u) {
  u.pendingGuid = u.selGuid;
  u.pendingTab = u.tab;
  u.pending = u.sync.db().importSettings(u.selGuid, u.tab);
  u.previewKey.clear();
}

static bool isProjectTab(const AssetUi& u, const std::string& tab) { return tab.empty() || tab == u.cookTarget; }

// F5.06: the target's import rules (module overrides + ConsoleTarget limits).
static const ImportRules& rulesFor(App& a, AssetUi& u) {
  static ImportRules rules;
  static std::string key;
  const std::string k = u.root.string() + "|" + u.cookTarget + "|" + std::to_string(u.ct.maxTextureSize) + "|" + std::to_string(u.ct.audioSampleRate) +
                        "|" + std::to_string(u.ct.textureFormats.size()) + "|" + std::to_string(u.ct.features.size());
  if (k != key) { key = k; rules = effectiveImportRules(a.targetModule(), u.ct); }
  return rules;
}

static std::vector<ImportField> fieldsFor(App& a, AssetUi& u, const AssetEntry& e, const std::string& tab) {
  return importFields(e.kind, u.ct, isProjectTab(u, tab), &rulesFor(a, u));
}

void assetInspectSetTab(App& a, const std::string& tab) {
  (void)a;
  AssetUi& u = assetUi();
  u.tab = tab == "default" ? std::string() : tab;
  u.forceTab = true;
}

static json parseValue(const ImportField& f, const std::string& v, bool& ok) {
  ok = true;
  if (f.type == "bool") {
    if (v == "true" || v == "1") return true;
    if (v == "false" || v == "0") return false;
    ok = false;
    return json();
  }
  if (f.type == "float") return std::atof(v.c_str());
  if (f.type == "enum") {
    if (std::find(f.options.begin(), f.options.end(), v) == f.options.end()) { ok = false; return json(); }
    if (f.def.is_number()) return std::atoi(v.c_str());
    return v;
  }
  return std::atoi(v.c_str());
}

static void setPending(AssetUi& u, const ImportField& f, const json& v) {
  // On the Default tab a value equal to the default is not stored: .meta files stay small and old cooks stay byte-identical.
  if (u.tab.empty() && v == f.def) u.pending.erase(f.key);
  else u.pending[f.key] = v;
  u.previewKey.clear();
}

bool assetInspectSetField(App& a, const std::string& key, const std::string& value) {
  AssetUi& u = assetUi();
  const AssetEntry* e = selected(u);
  if (!e) return false;
  if (u.pendingGuid != u.selGuid || u.pendingTab != u.tab) loadPending(u);
  for (const ImportField& f : fieldsFor(a, u, *e, u.tab))
    if (f.key == key) {
      bool ok = false;
      json v = parseValue(f, value, ok);
      if (!ok) return false;
      setPending(u, f, v);
      return true;
    }
  return false;
}

static void reimport(App& a, const std::string& guid) {
  AssetUi& u = assetUi();
  (void)a;
  if (const AssetEntry* e = u.sync.db().findGuid(guid)) {
    const AssetEntry copy = *e;
    u.sync.importAsset(copy, true);
    u.thumbs.invalidate(guid);
    u.vramKeyGen = -1;
  }
}

bool assetInspectApply(App& a) {
  AssetUi& u = assetUi();
  if (u.pendingGuid.empty()) return false;
  const json saved = u.sync.db().importSettings(u.pendingGuid, u.pendingTab);
  if (saved == u.pending) return true;
  if (!u.sync.db().setImportSettings(u.pendingGuid, u.pendingTab, u.pending)) {
    EditorLog::instance().error("asset: cannot write the .meta of " + u.sync.db().describeGuid(u.pendingGuid));
    return false;
  }
  u.undo.push_back({u.pendingGuid, u.pendingTab, saved, u.pending});
  u.redo.clear();
  EditorLog::instance().info("asset: import settings applied to " + u.sync.db().describeGuid(u.pendingGuid) +
                             (u.pendingTab.empty() ? " (default)" : " (" + u.pendingTab + ")"));
  reimport(a, u.pendingGuid);
  return true;
}

void assetInspectRevert(App& a) {
  (void)a;
  AssetUi& u = assetUi();
  if (!u.selGuid.empty()) loadPending(u);
}

bool assetInspectUndo(App& a, bool redo) {
  AssetUi& u = assetUi();
  std::vector<ImportUndo>& from = redo ? u.redo : u.undo;
  if (from.empty()) return false;
  ImportUndo r = from.back();
  from.pop_back();
  if (!u.sync.db().setImportSettings(r.guid, r.tab, redo ? r.after : r.before)) return false;
  (redo ? u.undo : u.redo).push_back(r);
  if (u.pendingGuid == r.guid && u.pendingTab == r.tab) u.pending = redo ? r.after : r.before;
  reimport(a, r.guid);
  return true;
}

static std::string kb(uint64_t b) {
  char t[48];
  if (b >= 1024 * 1024) std::snprintf(t, sizeof t, "%.2f MB", b / 1048576.0);
  else std::snprintf(t, sizeof t, "%.1f KB", b / 1024.0);
  return t;
}

static void drawCost(const AssetCost& c, const char* targetName) {
  ImGui::SeparatorText((std::string("Cost on ") + targetName).c_str());
  if (!c.valid && c.notes.empty()) { ImGui::TextDisabled("not available"); return; }
  if (c.valid) {
    ImGui::TextWrapped("%s", c.detail.c_str());
    ImGui::Text("VRAM %s (%llu bytes)   RAM %s (%llu bytes)", kb(c.vramBytes).c_str(), static_cast<unsigned long long>(c.vramBytes), kb(c.ramBytes).c_str(),
                static_cast<unsigned long long>(c.ramBytes));
  }
  for (const std::string& n : c.notes) {
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("warn"));
    ImGui::TextWrapped("[!] %s", n.c_str());
    ImGui::PopStyleColor();
  }
}

static void drawModelCost(App& a, AssetUi& u, const AssetEntry& e) {
  const std::string target = u.tab.empty() ? u.cookTarget : u.tab;
  const bool projectTab = isProjectTab(u, u.tab);
  const ImportRules& rules = rulesFor(a, u);
  const json cur = u.pending.contains("vertexPrecision") ? u.pending["vertexPrecision"] : json();
  const std::string vp = cur.is_string() ? cur.get<std::string>() : (rules.vertexPrecisions.empty() ? std::string("float") : rules.vertexPrecisions.back());
  const bool compact = projectTab && rules.vertexPrecisions.size() > 1;
  static std::string key;
  static AssetCost cost;
  const std::string k = e.guid + "|" + vp + "|" + std::to_string(compact) + "|" + std::to_string(u.sync.generation());
  if (k != key) { key = k; cost = modelCost(e.path, u.ct, vp, compact); }
  const TargetModule* tm = a.targetModule();
  drawCost(cost, projectTab && tm ? std::string(tm->displayName()).c_str() : target.c_str());
}

static void drawPreview(App& a, AssetUi& u, const AssetEntry& e) {
  if (e.kind == AssetKind::Model) { drawModelCost(a, u, e); return; }
  if (e.kind != AssetKind::Texture && e.kind != AssetKind::Audio) return;
  const std::string target = u.tab.empty() ? u.cookTarget : u.tab;
  json meta = e.meta;
  if (u.tab.empty()) meta["importSettings"] = u.pending;
  else {
    if (!meta.contains("targetOverrides") || !meta["targetOverrides"].is_object()) meta["targetOverrides"] = json::object();
    meta["targetOverrides"][u.tab] = u.pending;
  }
  const std::string text = effectiveImportSettings(meta, target);
  const std::string key = e.guid + "|" + target + "|" + text + "|" + std::to_string(u.sync.generation());
  if (key != u.previewKey) {
    u.previewKey = key;
    u.preview = u.sync.importer().import(e, target, text, false);
    if (u.preview.ok && e.kind == AssetKind::Texture) {
      const std::vector<uint8_t> rgba = cookedTextureToRgba(u.preview.tex);
      u.previewImg.upload(rgba.data(), static_cast<int>(u.preview.tex.width), static_cast<int>(u.preview.tex.height), true);
    }
  }
  const ImportOutcome& o = u.preview;
  ImGui::SeparatorText("Cooked result");
  if (!o.ok) {
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
    ImGui::TextWrapped("Error: %s", o.error.c_str());
    ImGui::PopStyleColor();
    return;
  }
  if (e.kind == AssetKind::Texture) {
    const float maxW = ImGui::GetContentRegionAvail().x - 8;
    float w = static_cast<float>(u.previewImg.w), h = static_cast<float>(u.previewImg.h);
    const float s = std::min(1.0f, std::min(maxW / std::max(w, 1.0f), 160.0f / std::max(h, 1.0f)));
    const float zoom = (w * 2 <= maxW && h * 2 <= 160) ? 2.0f : s;  // small sprites at 2x
    const ImVec2 sz(w * zoom, h * zoom);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ImGui::GetColorU32(brandToken("surface-2")));
    ImGui::Image(u.previewImg.id(), sz);
    dl->AddRect(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ImGui::GetColorU32(brandToken("border-strong")));
    ImGui::Text("%u x %u  (source %u x %u)", o.tex.width, o.tex.height, o.srcWidth, o.srcHeight);
    ImGui::Text("Cooked: %s", cookedTextureDescription(o.tex).c_str());
    ImGui::Text("Size: %s (%llu bytes)", kb(o.cookedBytes).c_str(), static_cast<unsigned long long>(o.cookedBytes));
    if (isProjectTab(u, u.tab) && u.ct.vramBytes) {
      const double pct = 100.0 * o.cookedBytes / u.ct.vramBytes;
      const bool over = o.cookedBytes > u.ct.vramBytes;
      if (over) {
        ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
        ImGui::Text("[!] Over budget: %s of %s VRAM", kb(o.cookedBytes).c_str(), kb(u.ct.vramBytes).c_str());
        ImGui::PopStyleColor();
      } else {
        ImGui::Text("VRAM: %s of %s (%.1f%%)", kb(o.cookedBytes).c_str(), kb(u.ct.vramBytes).c_str(), pct);
      }
    }
    if (isProjectTab(u, u.tab)) {
      const TargetModule* tm = a.targetModule();
      drawCost(textureCost(o, rulesFor(a, u)), tm ? std::string(tm->displayName()).c_str() : u.cookTarget.c_str());
    }
  } else {
    ImGui::Text("%u Hz, %u channel(s), %u frames", o.audioRate, o.audioChannels, o.audioFrames);
    ImGui::Text("Cooked: %s", kb(o.cookedBytes).c_str());
    if (isProjectTab(u, u.tab)) {
      const TargetModule* tm = a.targetModule();
      drawCost(audioCost(o), tm ? std::string(tm->displayName()).c_str() : u.cookTarget.c_str());
    }
  }
}

bool drawAssetInspector(App& a) {
  AssetUi& u = assetUi();
  if (u.selGuid.empty()) return false;
  if (a.doc.selected() != u.selDocObject) { u.selGuid.clear(); return false; }  // the scene selection moved on
  const AssetEntry* ep = selected(u);
  if (!ep) { u.selGuid.clear(); return false; }
  const AssetEntry& e = *ep;
  if (u.pendingGuid != u.selGuid || u.pendingTab != u.tab) loadPending(u);

  ImGui::PushFont(a.fonts.uiBold, 0.0f);
  ImGui::TextUnformatted(e.path.filename().string().c_str());
  ImGui::PopFont();
  ImGui::TextDisabled("%s  -  %s", assetKindName(e.kind), e.rel.c_str());
  ImGui::TextDisabled("GUID %s", e.guid.c_str());
  if (ImGui::IsItemClicked()) ImGui::SetClipboardText(e.guid.c_str());
  ImGui::TextDisabled("%s", kb(e.size).c_str());
  if (e.kind == AssetKind::Texture) {  // F4.08
    if (ImGui::Button("Open Sprite Editor", ImVec2(-FLT_MIN, 0))) openSpriteEditor(a, e.guid);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slice this texture into named sprites (grid or manual rectangles, pivots)");
  } else if (e.kind == AssetKind::Tilemap) {
    if (ImGui::Button("Open Tile Palette", ImVec2(-FLT_MIN, 0))) openTilePalette(a, e.guid);
  }

  const std::vector<ImportField> base = fieldsFor(a, u, e, u.tab);
  if (base.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled("No import settings for this kind of asset.");
    return true;
  }
  ImGui::SeparatorText("Import settings");
  // target tabs: Default, then the project's target, then the platform builds
  if (ImGui::BeginTabBar("##targets", ImGuiTabBarFlags_FittingPolicyScroll)) {
    std::vector<std::string> ids = {""};
    for (const std::string& t : buildTargetIds(u.cookTarget)) ids.push_back(t);
    for (const std::string& id : ids) {
      ImGuiTabItemFlags fl = ImGuiTabItemFlags_None;
      if (u.forceTab && id == u.tab) fl |= ImGuiTabItemFlags_SetSelected;
      std::string label = id.empty() ? "Default" : id;
      const bool hasOverride = !id.empty() && !u.sync.db().importSettings(e.guid, id).empty();
      if (hasOverride) label += " *";
      if (ImGui::BeginTabItem((label + "###tab_" + id).c_str(), nullptr, fl)) {
        if (u.forceTab) { if (u.tab == id) u.forceTab = false; }  // the requested tab is now the selected one
        else if (u.tab != id) { u.tab = id; loadPending(u); }
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  const bool defaultTab = u.tab.empty();
  if (isProjectTab(u, u.tab)) {  // F5.06: what the target takes (ImportRules)
    const ImportRules& r = rulesFor(a, u);
    std::string line = "Target limits:";
    if (e.kind == AssetKind::Texture) {
      if (r.maxTextureSize) line += " textures up to " + std::to_string(r.maxTextureSize) + " px (larger ones are downscaled)";
      if (r.pow2Warn) line += ", stored at power-of-two sizes";
      if (!r.clutDepths.empty()) {
        line += ", palettes";
        for (int d : r.clutDepths) line += " " + std::to_string(d) + "-bit";
      }
    } else if (e.kind == AssetKind::Audio) {
      line += " sample rates";
      for (int hz : r.audioRates) line += " " + std::to_string(hz);
      line += " Hz";
    } else if (e.kind == AssetKind::Model) {
      line += " vertices";
      for (const std::string& v : r.vertexPrecisions) line += " " + v;
      if (u.ct.maxBonesPerDraw) line += ", " + std::to_string(u.ct.maxBonesPerDraw) + " bones per draw";
      if (u.ct.maxSkinnedJoints) line += ", " + std::to_string(u.ct.maxSkinnedJoints) + " joints per skin";
    }
    ImGui::TextDisabled("%s.", line.c_str());
  }
  if (!defaultTab)
    ImGui::TextWrapped("Override for '%s'. Tick a field to replace the default for this target only.", u.tab.c_str());
  const json defaults = u.sync.db().importSettings(e.guid, "");
  if (ImGui::BeginTable("##fields", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 110);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    for (const ImportField& f : base) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::PushID(f.key.c_str());
      bool overridden = defaultTab || u.pending.contains(f.key);
      if (!defaultTab) {
        if (ImGui::Checkbox("##ov", &overridden)) {
          if (overridden) u.pending[f.key] = fieldValue(f, defaults);
          else u.pending.erase(f.key);
          u.previewKey.clear();
        }
        ImGui::SameLine();
      }
      ImGui::TextUnformatted(f.label.c_str());
      if (!f.help.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.help.c_str());
      ImGui::TableNextColumn();
      const json cur = defaultTab ? fieldValue(f, u.pending) : (u.pending.contains(f.key) ? u.pending[f.key] : fieldValue(f, defaults));
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::BeginDisabled(!overridden);
      if (f.type == "enum") {
        std::string curS = cur.is_number() ? std::to_string(cur.get<int>()) : cur.get<std::string>();
        if (ImGui::BeginCombo("##v", curS.c_str())) {
          for (const std::string& o : f.options)
            if (ImGui::Selectable(o.c_str(), o == curS)) {
              bool ok = false;
              setPending(u, f, parseValue(f, o, ok));
            }
          ImGui::EndCombo();
        }
      } else if (f.type == "bool") {
        bool b = cur.is_boolean() && cur.get<bool>();
        if (ImGui::Checkbox("##v", &b)) setPending(u, f, b);
      } else if (f.type == "float") {
        float v = cur.is_number() ? cur.get<float>() : 0.0f;
        if (ImGui::DragFloat("##v", &v, 0.01f, 0.0001f, 1000.0f, "%.3f")) setPending(u, f, v);
      }
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  const bool dirty = u.pending != u.sync.db().importSettings(e.guid, u.tab);
  ImGui::BeginDisabled(!dirty);
  ImGui::PushStyleColor(ImGuiCol_Button, brandToken("primary"));
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("on-primary"));
  if (ImGui::Button("Apply")) assetInspectApply(a);
  ImGui::PopStyleColor(2);
  ImGui::SameLine();
  if (ImGui::Button("Revert")) assetInspectRevert(a);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(u.undo.empty());
  if (ImGui::Button("Undo")) assetInspectUndo(a, false);
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(u.redo.empty());
  if (ImGui::Button("Redo")) assetInspectUndo(a, true);
  ImGui::EndDisabled();
  if (dirty) { ImGui::SameLine(); ImGui::TextColored(brandToken("warn"), "Unsaved changes"); }
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) assetInspectUndo(a, false);
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) assetInspectUndo(a, true);
  }
  drawPreview(a, u, e);
  return true;
}

}  // namespace editor
}  // namespace rensga
