// Project window (F3.02): folder tree + thumbnail grid over the asset database, search ("name", "t:texture"), create
// folder / script, rename, delete with confirm. Assets are dragged with the payload RENSGA_ASSET_GUID (see
// editor/core asset_database.h). The asset database lives in asset_ui.cpp's singleton; this file only draws it.
#include <algorithm>
#include <cstring>
#include <set>

#include "app.h"
#include "panel_registry.h"
#include "panels/asset_ui.h"
#include "panels/tile_tools.h"
#include "rensga/editor/asset_stubs.h"
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
    if (e.path().extension() == ".meta" || (!n.empty() && n[0] == '.')) continue;  // sidecars and hidden files are noise
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

static std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// "player t:texture": every word must match; "t:<kind>" matches the asset kind, other words the file name.
static bool matchesQuery(const AssetEntry& e, const std::string& query) {
  const std::string name = lower(e.path.filename().string());
  size_t i = 0;
  while (i < query.size()) {
    size_t j = query.find(' ', i);
    if (j == std::string::npos) j = query.size();
    const std::string w = lower(query.substr(i, j - i));
    i = j + 1;
    if (w.empty()) continue;
    if (w.compare(0, 2, "t:") == 0) {
      if (w.size() > 2 && std::string(assetKindName(e.kind)).find(w.substr(2)) != 0) return false;
    } else if (name.find(w) == std::string::npos) {
      return false;
    }
  }
  return true;
}

static const AssetNode* findNode(const AssetNode& n, const fs::path& p) {
  if (n.path == p) return &n;
  for (const AssetNode& c : n.children)
    if (c.dir)
      if (const AssetNode* r = findNode(c, p)) return r;
  return nullptr;
}

static const char* kindGlyph(AssetKind k) {
  switch (k) {
    case AssetKind::Texture: return "IMG";
    case AssetKind::Audio: return "SND";
    case AssetKind::Model: return "MDL";
    case AssetKind::Scene: return "SCN";
    case AssetKind::Script: return "C++";
    case AssetKind::Primitive: return "PRM";
    case AssetKind::Tilemap: return "MAP";
    case AssetKind::Animation: return "ANM";
    case AssetKind::Font: return "FNT";
    case AssetKind::Data: return "JSN";
    case AssetKind::Prefab: case AssetKind::Material: case AssetKind::AnimatorController:
    case AssetKind::PropertyClip: case AssetKind::DataAsset: {
      const StubKind* sk = stubKindFor(k);
      return sk ? sk->glyph : "...";
    }
    default: return "...";
  }
}

// F6: Create > Prefab / Material / Animator Controller / Property Clip / Data Asset (stubs from editor/core asset_stubs.h).
static void drawCreateStubItems(App& a) {
  int n = 0;
  const StubKind* kinds = stubKinds(&n);
  for (int i = 0; i < n; ++i)
    if (ImGui::MenuItem(kinds[i].label)) createAssetFromMenu(a, kinds[i].label, std::string());
}

bool createAssetFromMenu(App& a, const std::string& labelIn, const std::string& name) {
  AssetUi& u = assetUi();
  std::string label = labelIn;  // --ui-event words cannot hold spaces: Animator_Controller
  for (char& ch : label) if (ch == '_') ch = ' ';
  int n = 0;
  const StubKind* kinds = stubKinds(&n);
  for (int i = 0; i < n; ++i) {
    if (label != kinds[i].label) continue;
    fs::path made;
    std::string err;
    if (!createAssetStub(u.sync.db(), kinds[i].kind, a.project.root / u.folder, name, made, err)) {
      EditorLog::instance().warn("create " + label + " failed: " + err);
      u.errorText = err;
      return false;
    }
    a.assetsStamp = -100;
    if (const AssetEntry* e = u.sync.db().findPath(made)) {
      u.selGuid = e->guid;
      std::printf("asset: created %s (%s)\n", e->rel.c_str(), assetKindName(e->kind));
      EditorLog::instance().info("asset: created " + e->rel);
    }
    return true;
  }
  EditorLog::instance().warn("create: unknown asset kind '" + label + "'");
  return false;
}

static std::string relOf(App& a, const fs::path& p) {
  std::error_code ec;
  fs::path r = fs::relative(p, a.project.root, ec);
  return ec ? p.generic_string() : r.generic_string();
}

static void moveAsset(App& a, const std::string& guid, const fs::path& toDir) {
  AssetUi& u = assetUi();
  const AssetEntry* e = u.sync.db().findGuid(guid);
  if (!e) return;
  std::string err;
  const fs::path from = e->path;
  if (!u.sync.db().move(from, toDir, err)) { EditorLog::instance().warn("move failed: " + err); u.errorText = err; }
  else { EditorLog::instance().info("asset: moved " + relOf(a, from) + " -> " + relOf(a, toDir)); a.assetsStamp = -100; }
}

static void drawTreeNode(App& a, const AssetNode& n, bool root) {
  AssetUi& u = assetUi();
  const std::string rel = root ? "Assets" : relOf(a, n.path);
  bool hasDirs = false;
  for (const AssetNode& c : n.children) hasDirs |= c.dir;
  ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
  if (!hasDirs) fl |= ImGuiTreeNodeFlags_Leaf;
  if (u.folder == rel) fl |= ImGuiTreeNodeFlags_Selected;
  const bool open = ImGui::TreeNodeEx((n.name + "##" + rel).c_str(), fl);
  if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) u.folder = rel;
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kAssetGuidPayload)) moveAsset(a, static_cast<const char*>(p->Data), root ? a.project.root / "Assets" : n.path);
    ImGui::EndDragDropTarget();
  }
  if (open) {
    for (const AssetNode& c : n.children)
      if (c.dir) drawTreeNode(a, c, false);
    ImGui::TreePop();
  }
}

struct Tile {
  std::string name;       // label
  std::string key;        // unique id
  AssetKind kind = AssetKind::Folder;
  std::string guid;       // empty for folders
  fs::path path;
};

static bool openAsset(App& a, const Tile& t) {
  AssetUi& u = assetUi();
  if (t.kind == AssetKind::Folder) { u.folder = relOf(a, t.path); u.search[0] = 0; return true; }
  if (t.kind == AssetKind::Scene) return openSceneFile(a, t.path);
  if (t.kind == AssetKind::Texture && !t.guid.empty()) { openSpriteEditor(a, t.guid); return true; }   // F4.08
  if (t.kind == AssetKind::Tilemap && !t.guid.empty()) { openTilePalette(a, t.guid); return true; }
  if (t.kind == AssetKind::Script) {
    if (!openInExternalEditor(t.path)) EditorLog::instance().warn("cannot open " + t.path.string() + " with " + externalEditorCommand());
    return true;
  }
  return false;
}

static void drawTile(App& a, const Tile& t, float cell, float thumb) {
  AssetUi& u = assetUi();
  ImGui::PushID(t.key.c_str());
  const ImVec2 p0 = ImGui::GetCursorScreenPos();
  const bool selected = !t.guid.empty() && t.guid == u.selGuid;
  ImGui::InvisibleButton("##tile", ImVec2(cell, thumb + ImGui::GetTextLineHeight() * 2 + 10));
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 t0(p0.x + (cell - thumb) * 0.5f, p0.y + 3), t1(t0.x + thumb, t0.y + thumb);
  if (selected) dl->AddRectFilled(p0, ImVec2(p0.x + cell, p0.y + thumb + ImGui::GetTextLineHeight() * 2 + 10), ImGui::GetColorU32(ImVec4(brandToken("primary").x, brandToken("primary").y, brandToken("primary").z, 0.28f)), 4);
  else if (hovered) dl->AddRectFilled(p0, ImVec2(p0.x + cell, p0.y + thumb + ImGui::GetTextLineHeight() * 2 + 10), ImGui::GetColorU32(brandToken("border")), 4);
  dl->AddRectFilled(t0, t1, ImGui::GetColorU32(brandToken("surface")), 3);
  ImTextureID tex = 0;
  if (t.kind == AssetKind::Texture) tex = u.thumbs.get(t.guid, t.path);
  if (tex) {
    // checkerboard under transparent sprites would be nicer; the tile background already contrasts
    const GlImage* gi = nullptr;
    (void)gi;
    dl->AddImage(tex, ImVec2(t0.x + 2, t0.y + 2), ImVec2(t1.x - 2, t1.y - 2));
  } else if (t.kind == AssetKind::Folder) {
    const ImU32 c = ImGui::GetColorU32(brandToken("text-2"));
    dl->AddRectFilled(ImVec2(t0.x + 8, t0.y + thumb * 0.28f), ImVec2(t0.x + thumb * 0.45f, t0.y + thumb * 0.4f), c, 2);
    dl->AddRectFilled(ImVec2(t0.x + 8, t0.y + thumb * 0.36f), ImVec2(t1.x - 8, t1.y - thumb * 0.2f), c, 3);
  } else {
    const char* g = kindGlyph(t.kind);
    const ImVec2 ts = ImGui::CalcTextSize(g);
    dl->AddText(ImVec2(t0.x + (thumb - ts.x) * 0.5f, t0.y + (thumb - ts.y) * 0.5f), ImGui::GetColorU32(brandToken("text-2")), g);
  }
  // name, up to two lines, clipped
  const ImVec2 n0(p0.x + 2, t1.y + 4);
  dl->PushClipRect(n0, ImVec2(p0.x + cell - 2, n0.y + ImGui::GetTextLineHeight() * 2 + 2), true);
  dl->AddText(nullptr, 0, n0, ImGui::GetColorU32(ImGuiCol_Text), t.name.c_str(), nullptr, cell - 4);
  dl->PopClipRect();

  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !t.guid.empty()) selectAsset(a, t.guid);
  if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openAsset(a, t);
  if (!t.guid.empty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
    char payload[37] = {};
    std::snprintf(payload, sizeof payload, "%s", t.guid.c_str());
    ImGui::SetDragDropPayload(kAssetGuidPayload, payload, sizeof payload);  // 36 chars + NUL
    ImGui::TextUnformatted(t.name.c_str());
    ImGui::EndDragDropSource();
  }
  if (t.kind == AssetKind::Folder && ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kAssetGuidPayload)) moveAsset(a, static_cast<const char*>(p->Data), t.path);
    ImGui::EndDragDropTarget();
  }
  if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !t.guid.empty()) selectAsset(a, t.guid);
  if (ImGui::BeginPopupContextItem("tilectx")) {
    if (ImGui::MenuItem("Open")) openAsset(a, t);
    if (ImGui::MenuItem("Rename", "F2")) { u.renameRel = relOf(a, t.path); std::snprintf(u.nameBuf, sizeof u.nameBuf, "%s", t.path.filename().string().c_str()); u.openRename = true; }
    if (ImGui::MenuItem("Delete")) { u.deleteRel = relOf(a, t.path); u.openDelete = true; }
    if (!t.guid.empty() && ImGui::MenuItem("Copy GUID")) ImGui::SetClipboardText(t.guid.c_str());
    ImGui::EndPopup();
  }
  if (hovered && !t.guid.empty()) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(relOf(a, t.path).c_str());
    ImGui::TextDisabled("%s  %s", assetKindName(t.kind), t.guid.c_str());
    ImGui::EndTooltip();
  }
  ImGui::PopID();
}

static void drawAssetDialogs(App& a) {
  AssetUi& u = assetUi();
  if (u.openRename) { ImGui::OpenPopup("Rename asset"); u.openRename = false; }
  if (u.openDelete) { ImGui::OpenPopup("Delete asset"); u.openDelete = false; }
  if (u.openNewFolder) { ImGui::OpenPopup("New folder"); u.openNewFolder = false; }
  const ImVec2 c = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Rename asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextDisabled("%s", u.renameRel.c_str());
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##name", u.nameBuf, sizeof u.nameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    if (!u.errorText.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger")); ImGui::TextWrapped("Error: %s", u.errorText.c_str()); ImGui::PopStyleColor(); }
    if (enter || ImGui::Button("Rename")) {
      std::string err;
      if (u.sync.db().rename(a.project.root / u.renameRel, u.nameBuf, err)) { u.errorText.clear(); a.assetsStamp = -100; ImGui::CloseCurrentPopup(); }
      else u.errorText = err;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) { u.errorText.clear(); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("New folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextDisabled("in %s", u.folder.c_str());
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    const bool enter = ImGui::InputText("##name", u.nameBuf, sizeof u.nameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    if (!u.errorText.empty()) { ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger")); ImGui::TextWrapped("Error: %s", u.errorText.c_str()); ImGui::PopStyleColor(); }
    if (enter || ImGui::Button("Create")) {
      std::string err;
      if (u.sync.db().createFolder(a.project.root / u.folder, u.nameBuf, err)) { u.errorText.clear(); a.assetsStamp = -100; ImGui::CloseCurrentPopup(); }
      else u.errorText = err;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) { u.errorText.clear(); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Delete asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete '%s'?", u.deleteRel.c_str());
    ImGui::TextDisabled("Scenes that reference it will show \"Missing (<guid>)\". This cannot be undone.");
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
    const bool del = ImGui::Button("Delete");
    ImGui::PopStyleColor();
    if (del) {
      std::string err;
      if (u.sync.db().remove(a.project.root / u.deleteRel, err)) { u.selGuid.clear(); a.assetsStamp = -100; }
      else EditorLog::instance().warn("delete failed: " + err);
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}

void drawProjectPanel(App& a) {
  AssetUi& u = assetUi();
  assetUiTick(a);  // the sync runs even when the window is closed
  drawVramMapPanel(a);
  if (!a.showProject) return;
  if (!ImGui::Begin("Project", nullptr)) { ImGui::End(); return; }
  if (!a.loaded) {
    ImGui::TextDisabled("No project loaded");
    ImGui::End();
    return;
  }
  if (ImGui::GetTime() - a.assetsStamp > 2.0) { a.refreshAssets(); a.assetsStamp = ImGui::GetTime(); }

  // toolbar
  if (ImGui::Button("Create") || u.openCreateMenu) { ImGui::OpenPopup("createmenu"); u.openCreateMenu = false; }
  if (ImGui::BeginPopup("createmenu")) {
    if (ImGui::MenuItem("Folder")) { u.nameBuf[0] = 0; u.errorText.clear(); u.openNewFolder = true; }
    if (ImGui::MenuItem("C++ Script")) {
      std::string base = "NewScript", name = base;  // first free "NewScript", "NewScript1", ...
      std::set<std::string> none;
      for (int i = 1; !validateScriptName(a.project.root, name, none).empty() && i < 100; ++i) name = base + std::to_string(i);
      std::snprintf(a.scripts.newName, sizeof a.scripts.newName, "%s", name.c_str());
      a.scripts.openNewScript = true;
    }
    ImGui::Separator();
    drawCreateStubItems(a);
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##search", "Search (name, t:texture)", u.search, sizeof u.search);
  ImGui::SameLine();
  ImGui::TextDisabled("%s", u.search[0] ? "search results" : u.folder.c_str());
  if (u.sync.db().entries().empty()) { ImGui::SameLine(); ImGui::TextDisabled("(no assets)"); }

  // body: folder tree | grid
  const float treeW = 170;
  ImGui::BeginChild("##tree", ImVec2(treeW, 0), ImGuiChildFlags_Borders);
  drawTreeNode(a, a.assets, true);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##grid", ImVec2(0, 0), ImGuiChildFlags_Borders);
  std::vector<Tile> tiles;
  const std::string query = u.search;
  if (!query.empty()) {
    for (const AssetEntry& e : u.sync.db().entries())
      if (matchesQuery(e, query)) tiles.push_back({e.path.filename().string(), e.rel, e.kind, e.guid, e.path});
  } else {
    const fs::path dir = u.folder == "Assets" ? a.project.root / "Assets" : a.project.root / u.folder;
    const AssetNode* node = u.folder == "Assets" ? &a.assets : findNode(a.assets, dir);
    if (node)
      for (const AssetNode& c : node->children) {
        if (c.dir) { tiles.push_back({c.name, relOf(a, c.path), AssetKind::Folder, "", c.path}); continue; }
        const AssetEntry* e = u.sync.db().findPath(c.path);
        // a script shows by class name, like Unity
        std::string label = c.name;
        if (e && e->kind == AssetKind::Script && c.path.extension() == ".cpp") label = c.path.stem().string();
        tiles.push_back({label, relOf(a, c.path), e ? e->kind : assetKindOf(c.path), e ? e->guid : "", c.path});
      }
  }
  const float cell = 84, thumb = 64;
  const float availW = ImGui::GetContentRegionAvail().x;
  const int perRow = std::max(1, static_cast<int>(availW / (cell + 6)));
  int col = 0;
  for (const Tile& t : tiles) {
    if (col > 0) ImGui::SameLine(0, 6);
    drawTile(a, t, cell, thumb);
    if (++col >= perRow) col = 0;
  }
  if (tiles.empty()) ImGui::TextDisabled(query.empty() ? "This folder is empty." : "Nothing matches.");
  // background menu and shortcuts
  if (ImGui::BeginPopupContextWindow("proj_ctx", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
    if (ImGui::MenuItem("Create Folder")) { u.nameBuf[0] = 0; u.errorText.clear(); u.openNewFolder = true; }
    if (ImGui::MenuItem("Create C++ Script")) {
      std::string base = "NewScript", name = base;
      std::set<std::string> none;
      for (int i = 1; !validateScriptName(a.project.root, name, none).empty() && i < 100; ++i) name = base + std::to_string(i);
      std::snprintf(a.scripts.newName, sizeof a.scripts.newName, "%s", name.c_str());
      a.scripts.openNewScript = true;
    }
    ImGui::Separator();
    drawCreateStubItems(a);
    ImGui::EndPopup();
  }
  if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && !u.selGuid.empty()) {
    const AssetEntry* e = u.sync.db().findGuid(u.selGuid);
    if (e && ImGui::IsKeyPressed(ImGuiKey_F2, false)) { u.renameRel = e->rel; std::snprintf(u.nameBuf, sizeof u.nameBuf, "%s", e->path.filename().string().c_str()); u.openRename = true; }
    if (e && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) { u.deleteRel = e->rel; u.openDelete = true; }
  }
  ImGui::EndChild();
  drawAssetDialogs(a);
  ImGui::End();
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(project, "Project", drawProjectPanel, nullptr, nullptr, 110, 0, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
