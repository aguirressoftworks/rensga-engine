// F6.04 Editor 10: the Project Settings window. Tags and Layers (TagManager.json), Physics (Physics.json: gravity and the 3D collision
// matrix), Physics 2D (Physics2D.json: layers and the 2D matrix), Time (Time.json), Scenes in Build (Project.json) and Video (read only,
// from the ConsoleTarget). Files are written only when their tab changed, so an untouched setting never creates a file.
//
// --ui-event verbs (output lines start with "settings:"):
//   projsettings[:tags|physics|physics2d|time|scenes|video|off]   open the window on a tab / close it
//   psset:<key>=<value>   tag.add=<name> tag.remove=<name> layer.<n>=<name> p3.gravity=x,y,z p3.collide.<a>.<b>=0|1 p3.triggers=0|1
//                         p2.layer.add=<name> p2.collide.<a>.<b>=0|1 time.fixed=<s> time.maxdelta=<s> time.scale=<x>
//                         scene.add (the open scene) scene.enable.<i>=0|1 scene.move.<i>=<j> scene.remove.<i>
//   pssave | psrevert
//   psdump                every value, the validation result and the Video summary
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "app.h"
#include "panel_registry.h"
#include "rensga/editor/physics2d_settings.h"
#include "rensga/editor/project_settings.h"
#include "rensga/editor/target_module.h"

namespace rensga {
namespace editor {

namespace {

enum Tab { kTags, kPhysics, kPhysics2D, kTime, kScenes, kVideo, kTabCount };
const char* kTabNames[kTabCount] = {"Tags and Layers", "Physics", "Physics 2D", "Time", "Scenes in Build", "Video"};
const char* kTabIds[kTabCount] = {"tags", "physics", "physics2d", "time", "scenes", "video"};

struct PsState {
  bool open = false;
  bool loaded = false;
  std::string root;
  int tab = kTags, requestTab = -1;
  TagManagerSettings tags, savedTags;
  PhysicsSettings phys, savedPhys;
  Physics2DSettings p2, savedP2;
  TimeSettings time, savedTime;
  ProjectBuildSettings build, savedBuild;
  std::string error;
  char newTag[64] = {};
  char newLayer2d[32] = {};
};
PsState g_ps;

bool dirtyTab(int t) {
  switch (t) {
    case kTags: return g_ps.tags.toJson() != g_ps.savedTags.toJson();
    case kPhysics: return g_ps.phys.toJson() != g_ps.savedPhys.toJson();
    case kPhysics2D: return g_ps.p2.toJson() != g_ps.savedP2.toJson();
    case kTime: return g_ps.time.toJson() != g_ps.savedTime.toJson();
    case kScenes: return g_ps.build.toJson() != g_ps.savedBuild.toJson();
    default: return false;
  }
}

void reload(App& a) {
  std::string err;
  g_ps.root = a.project.root.string();
  g_ps.error.clear();
  auto note = [&](bool ok) { if (!ok) g_ps.error += err + "  "; err.clear(); };
  note(loadTagManager(a.project.root, g_ps.tags, err));
  note(loadPhysics(a.project.root, g_ps.phys, err));
  note(loadPhysics2D(a.project.root, g_ps.p2, err));
  note(loadTime(a.project.root, g_ps.time, err));
  note(loadProjectBuild(a.project.root, g_ps.build, err));
  g_ps.savedTags = g_ps.tags; g_ps.savedPhys = g_ps.phys; g_ps.savedP2 = g_ps.p2; g_ps.savedTime = g_ps.time; g_ps.savedBuild = g_ps.build;
  g_ps.loaded = true;
}

std::vector<std::string> problemsOf(int t) {
  switch (t) {
    case kTags: return g_ps.tags.validate();
    case kPhysics: return g_ps.phys.validate();
    case kPhysics2D: return g_ps.p2.validate();
    case kTime: return g_ps.time.validate();
    case kScenes: return g_ps.build.validate();
    default: return {};
  }
}

bool saveAll(App& a, std::string& report) {
  bool ok = true;
  std::string err;
  auto one = [&](int t, bool (*save)(App&, std::string&), const char* file) {
    if (!dirtyTab(t)) return;
    const std::vector<std::string> pr = problemsOf(t);
    if (!pr.empty()) { ok = false; report += std::string(file) + ": " + pr[0] + "; "; return; }
    err.clear();
    if (save(a, err)) report += std::string(file) + " saved; "; else { ok = false; report += std::string(file) + ": " + err + "; "; }
  };
  one(kTags, [](App& ap, std::string& e) { bool k = saveTagManager(ap.project.root, g_ps.tags, e); if (k) g_ps.savedTags = g_ps.tags; return k; }, "TagManager.json");
  one(kPhysics, [](App& ap, std::string& e) { bool k = savePhysics(ap.project.root, g_ps.phys, e); if (k) g_ps.savedPhys = g_ps.phys; return k; }, "Physics.json");
  one(kPhysics2D, [](App& ap, std::string& e) { bool k = savePhysics2D(ap.project.root, g_ps.p2, e); if (k) g_ps.savedP2 = g_ps.p2; return k; }, "Physics2D.json");
  one(kTime, [](App& ap, std::string& e) { bool k = saveTime(ap.project.root, g_ps.time, e); if (k) g_ps.savedTime = g_ps.time; return k; }, "Time.json");
  one(kScenes, [](App& ap, std::string& e) { bool k = saveProjectBuild(ap.project.root, g_ps.build, e); if (k) g_ps.savedBuild = g_ps.build; return k; }, "Project.json");
  return ok;
}

std::string sceneName(App& a, const std::string& guid) {
  for (const Scene& s : a.project.scenes)
    if (s.guid == guid) {
      const size_t slash = s.path.find_last_of("/\\");
      return slash == std::string::npos ? s.path : s.path.substr(slash + 1);
    }
  return "(missing " + guid.substr(0, 8) + ")";
}

// Triangular collision matrix over `names` (empty names skipped). `get(a, b)` / `set(a, b, on)` use layer indexes.
template <class Get, class Set>
bool collisionMatrix(const char* id, const std::vector<std::pair<int, std::string>>& layers, Get get, Set set) {
  bool changed = false;
  const int n = static_cast<int>(layers.size());
  if (n == 0) return false;
  if (ImGui::BeginTable(id, n + 1, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("Layer");
    for (int c = 0; c < n; ++c) { char h[8]; std::snprintf(h, sizeof h, "%d", layers[static_cast<size_t>(n - 1 - c)].first); ImGui::TableSetupColumn(h); }
    ImGui::TableHeadersRow();
    for (int r = 0; r < n; ++r) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%d  %s", layers[static_cast<size_t>(r)].first, layers[static_cast<size_t>(r)].second.c_str());
      for (int c = 0; c < n; ++c) {
        ImGui::TableSetColumnIndex(c + 1);
        const int col = n - 1 - c;                       // Unity's staircase: the first row has every column, the last one only one
        if (col < r) continue;
        bool on = get(layers[static_cast<size_t>(r)].first, layers[static_cast<size_t>(col)].first);
        char cid[40];  // "##m%d_%d" with two full ints fits (-Wformat-truncation)
        std::snprintf(cid, sizeof cid, "##m%d_%d", r, col);
        if (ImGui::Checkbox(cid, &on)) { set(layers[static_cast<size_t>(r)].first, layers[static_cast<size_t>(col)].first, on); changed = true; }
      }
    }
    ImGui::EndTable();
  }
  return changed;
}

void drawSettings(App& a) {
  if (!g_ps.open || !a.loaded) return;
  if (!g_ps.loaded || g_ps.root != a.project.root.string()) reload(a);
  ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_Appearing);
  if (!ImGui::Begin("Project Settings", &g_ps.open)) { ImGui::End(); return; }
  bool anyDirty = false;
  for (int t = 0; t < kTabCount; ++t) anyDirty |= dirtyTab(t);
  if (ImGui::Button("Save")) { std::string rep; const bool ok = saveAll(a, rep); g_ps.error = ok ? std::string() : rep; if (!rep.empty()) EditorLog::instance().info("Project Settings: " + rep); }
  ImGui::SameLine();
  if (ImGui::Button("Revert")) reload(a);
  ImGui::SameLine();
  ImGui::TextColored(anyDirty ? brandToken("warn") : brandToken("text-2"), anyDirty ? "unsaved changes" : "saved");
  if (!g_ps.error.empty()) ImGui::TextColored(brandToken("danger"), "Error: %s", g_ps.error.c_str());
  ImGui::Separator();
  if (ImGui::BeginTabBar("##ps")) {
    for (int t = 0; t < kTabCount; ++t) {
      std::string label = std::string(kTabNames[t]) + (dirtyTab(t) ? " *" : "");
      ImGuiTabItemFlags fl = g_ps.requestTab == t ? ImGuiTabItemFlags_SetSelected : 0;
      const std::string id = label + "###tab" + kTabIds[t];
      if (ImGui::BeginTabItem(id.c_str(), nullptr, fl)) {
        g_ps.tab = t;
        ImGui::BeginChild("##body", ImVec2(0, 0), false);
        for (const std::string& p : problemsOf(t)) ImGui::TextColored(brandToken("danger"), "Error: %s", p.c_str());
        if (t == kTags) {
          ImGui::SeparatorText("Tags");
          ImGui::TextDisabled("Untagged is built in. An object with no tag has it.");
          for (size_t i = 0; i < g_ps.tags.tags.size();) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%zu", i);
            ImGui::SameLine(40);
            ImGui::TextUnformatted(g_ps.tags.tags[i].c_str());
            ImGui::SameLine(300);
            const bool rm = ImGui::SmallButton("Remove");
            ImGui::PopID();
            if (rm) g_ps.tags.tags.erase(g_ps.tags.tags.begin() + static_cast<long>(i)); else ++i;
          }
          ImGui::SetNextItemWidth(220);
          ImGui::InputText("##newtag", g_ps.newTag, sizeof g_ps.newTag);
          ImGui::SameLine();
          if (ImGui::Button("Add tag") && g_ps.newTag[0] && !g_ps.tags.hasTag(g_ps.newTag)) { g_ps.tags.tags.push_back(g_ps.newTag); g_ps.newTag[0] = 0; }
          ImGui::SeparatorText("Layers");
          ImGui::TextDisabled("32 layers. The number is what a scene stores; layer 0 is always Default.");
          for (int i = 0; i < kLayerCount; ++i) {
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Layer %2d", i);
            ImGui::SameLine(80);
            char buf[40];
            std::snprintf(buf, sizeof buf, "%s", g_ps.tags.layers[static_cast<size_t>(i)].c_str());
            ImGui::SetNextItemWidth(220);
            if (i == 0) ImGui::BeginDisabled();
            if (ImGui::InputText("##layer", buf, sizeof buf)) g_ps.tags.layers[static_cast<size_t>(i)] = buf;
            if (i == 0) ImGui::EndDisabled();
            ImGui::PopID();
          }
        } else if (t == kPhysics) {
          ImGui::SeparatorText("Gravity");
          float g3[3] = {g_ps.phys.gravity[0], g_ps.phys.gravity[1], g_ps.phys.gravity[2]};
          ImGui::SetNextItemWidth(260);
          if (ImGui::DragFloat3("m/s^2", g3, 0.05f, -1000, 1000)) for (int i = 0; i < 3; ++i) g_ps.phys.gravity[i] = g3[i];
          ImGui::Checkbox("Queries hit triggers", &g_ps.phys.queriesHitTriggers);
          ImGui::SeparatorText("Layer collision matrix");
          ImGui::TextDisabled("A checked box means the two layers collide. Layer names come from Tags and Layers.");
          std::vector<std::pair<int, std::string>> named;
          for (int i = 0; i < kLayerCount; ++i) if (!g_ps.tags.layers[static_cast<size_t>(i)].empty()) named.emplace_back(i, g_ps.tags.layers[static_cast<size_t>(i)]);
          collisionMatrix("##m3", named, [](int x, int y) { return g_ps.phys.collides(x, y); }, [](int x, int y, bool on) { g_ps.phys.setCollides(x, y, on); });
        } else if (t == kPhysics2D) {
          ImGui::SeparatorText("2D layers");
          ImGui::TextDisabled("Physics2D.json keeps its own layer list (by name).");
          for (size_t i = 0; i < g_ps.p2.layers.size();) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%zu  %s", i, g_ps.p2.layers[i].c_str());
            bool rm = false;
            if (i > 0) { ImGui::SameLine(300); rm = ImGui::SmallButton("Remove"); }
            ImGui::PopID();
            if (rm) {
              const std::string gone = g_ps.p2.layers[i];
              g_ps.p2.layers.erase(g_ps.p2.layers.begin() + static_cast<long>(i));
              for (size_t k = 0; k < g_ps.p2.ignore.size();) if (g_ps.p2.ignore[k].first == gone || g_ps.p2.ignore[k].second == gone) g_ps.p2.ignore.erase(g_ps.p2.ignore.begin() + static_cast<long>(k)); else ++k;
            } else ++i;
          }
          ImGui::SetNextItemWidth(220);
          ImGui::InputText("##newl2", g_ps.newLayer2d, sizeof g_ps.newLayer2d);
          ImGui::SameLine();
          if (ImGui::Button("Add layer") && g_ps.newLayer2d[0] && g_ps.p2.layerIndex(g_ps.newLayer2d) < 0 && g_ps.p2.layers.size() < 32) { g_ps.p2.layers.push_back(g_ps.newLayer2d); g_ps.newLayer2d[0] = 0; }
          ImGui::SeparatorText("Layer collision matrix");
          std::vector<std::pair<int, std::string>> named;
          for (size_t i = 0; i < g_ps.p2.layers.size(); ++i) named.emplace_back(static_cast<int>(i), g_ps.p2.layers[i]);
          collisionMatrix("##m2", named, [](int x, int y) { return g_ps.p2.collides(x, y); }, [](int x, int y, bool on) { g_ps.p2.setCollides(x, y, on); });
        } else if (t == kTime) {
          double fs = g_ps.time.fixedTimestep;
          ImGui::SetNextItemWidth(200);
          if (ImGui::InputDouble("Fixed timestep (s)", &fs, 0.001, 0.005, "%.6f")) g_ps.time.fixedTimestep = fs;
          ImGui::SameLine();
          ImGui::TextDisabled("= %.1f updates per second. The determinism checks use 1/60.", g_ps.time.fixedTimestep > 0 ? 1.0 / g_ps.time.fixedTimestep : 0.0);
          ImGui::SetNextItemWidth(200);
          ImGui::InputDouble("Maximum allowed timestep (s)", &g_ps.time.maximumDelta, 0.01, 0.1, "%.3f");
          ImGui::SetNextItemWidth(200);
          ImGui::InputDouble("Time scale", &g_ps.time.timeScale, 0.1, 1.0, "%.2f");
        } else if (t == kScenes) {
          ImGui::TextDisabled("The first enabled scene starts the game. Unchecked scenes are not cooked.");
          for (int i = 0; i < static_cast<int>(g_ps.build.scenes.size());) {
            BuildScene& s = g_ps.build.scenes[static_cast<size_t>(i)];
            ImGui::PushID(i);
            ImGui::Checkbox("##en", &s.enabled);
            ImGui::SameLine();
            ImGui::Text("%d  %s", i, sceneName(a, s.guid).c_str());
            ImGui::SameLine(360);
            bool rm = false;
            if (ImGui::SmallButton("Up")) g_ps.build.moveScene(i, i - 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("Down")) g_ps.build.moveScene(i, i + 1);
            ImGui::SameLine();
            rm = ImGui::SmallButton("Remove");
            ImGui::PopID();
            if (rm) g_ps.build.scenes.erase(g_ps.build.scenes.begin() + i); else ++i;
          }
          if (ImGui::Button("Add open scene") && a.doc.loaded()) g_ps.build.addScene(a.doc.scene().guid);
        } else {
          const ConsoleTargetData& ct = consoleTargetFor(a.project.root);
          ImGui::TextDisabled("Read only: these come from ProjectSettings/ConsoleTarget.json (the target).");
          ImGui::Text("Target:       %s", ct.profile.c_str());
          ImGui::Text("Resolution:   %u x %u  (the Canvas reference maps to this)", ct.width, ct.height);
          ImGui::Text("Refresh rate: %u Hz", ct.refreshHz);
          ImGui::Text("Scalar:       %s", ct.scalar.c_str());
          ImGui::Text("Depth:        %s", ct.depth.c_str());
          ImGui::Text("Framebuffer:  %u buffers, %u bpp, %u bit depth", ct.fbBuffers, ct.fbBpp, ct.fbDepthBits);
          ImGui::Text("Video memory: %.1f MB", static_cast<double>(ct.vramBytes) / (1024.0 * 1024.0));
          ImGui::Text("Memory:       %.1f MB", static_cast<double>(ct.ramBytes) / (1024.0 * 1024.0));
          ImGui::Text("Media:        %s", ct.media.c_str());
          std::string fm;
          for (const std::string& f : ct.textureFormats) fm += (fm.empty() ? "" : ", ") + f;
          ImGui::Text("Textures:     %s", fm.c_str());
        }
        ImGui::EndChild();
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }
  g_ps.requestTab = -1;
  ImGui::End();
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t at = 0;
  while (true) { size_t e = s.find(sep, at); out.push_back(s.substr(at, e == std::string::npos ? e : e - at)); if (e == std::string::npos) break; at = e + 1; }
  return out;
}

bool settingsHook(App& a, const std::string& act, const std::string& arg) {
  auto ensure = [&] { if (!g_ps.loaded || g_ps.root != a.project.root.string()) reload(a); };
  if (act == "projsettings") {
    if (arg == "off") g_ps.open = false;
    else {
      g_ps.open = true;
      ensure();
      for (int t = 0; t < kTabCount; ++t) if (arg == kTabIds[t]) g_ps.requestTab = t;
    }
    std::printf("settings: window %s tab=%s\n", g_ps.open ? "open" : "closed", arg.empty() ? "tags" : arg.c_str());
  } else if (act == "psset") {
    ensure();
    const size_t eq = arg.find('=');
    const std::string k = arg.substr(0, eq), v = eq == std::string::npos ? std::string() : arg.substr(eq + 1);
    bool ok = true;
    const std::vector<std::string> kp = split(k, '.');
    auto num = [&](size_t i) { return kp.size() > i ? std::atoi(kp[i].c_str()) : -1; };
    if (k == "tag.add") ok = !v.empty() && !g_ps.tags.hasTag(v) && (g_ps.tags.tags.push_back(v), true);
    else if (k == "tag.remove") { auto it = std::find(g_ps.tags.tags.begin(), g_ps.tags.tags.end(), v); ok = it != g_ps.tags.tags.end(); if (ok) g_ps.tags.tags.erase(it); }
    else if (kp.size() == 2 && kp[0] == "layer" && num(1) >= 0 && num(1) < kLayerCount) g_ps.tags.layers[static_cast<size_t>(num(1))] = v;
    else if (k == "p3.gravity") { float g[3]; ok = std::sscanf(v.c_str(), "%f,%f,%f", &g[0], &g[1], &g[2]) == 3; if (ok) for (int i = 0; i < 3; ++i) g_ps.phys.gravity[i] = g[i]; }
    else if (k == "p3.triggers") g_ps.phys.queriesHitTriggers = v == "1";
    else if (kp.size() == 4 && kp[0] == "p3" && kp[1] == "collide") g_ps.phys.setCollides(num(2), num(3), v == "1");
    else if (k == "p2.layer.add") ok = !v.empty() && g_ps.p2.layerIndex(v) < 0 && (g_ps.p2.layers.push_back(v), true);
    else if (kp.size() == 4 && kp[0] == "p2" && kp[1] == "collide") g_ps.p2.setCollides(num(2), num(3), v == "1");
    else if (k == "time.fixed") g_ps.time.fixedTimestep = std::atof(v.c_str());
    else if (k == "time.maxdelta") g_ps.time.maximumDelta = std::atof(v.c_str());
    else if (k == "time.scale") g_ps.time.timeScale = std::atof(v.c_str());
    else if (k == "scene.add") {
      ok = a.doc.loaded() && g_ps.build.addScene(a.doc.scene().guid);
    }
    else if (kp.size() == 3 && kp[0] == "scene" && kp[1] == "enable") ok = num(2) >= 0 && num(2) < static_cast<int>(g_ps.build.scenes.size()) && (g_ps.build.scenes[static_cast<size_t>(num(2))].enabled = v == "1", true);
    else if (kp.size() == 3 && kp[0] == "scene" && kp[1] == "move") ok = g_ps.build.moveScene(num(2), std::atoi(v.c_str()));
    else if (kp.size() == 3 && kp[0] == "scene" && kp[1] == "remove") ok = num(2) >= 0 && num(2) < static_cast<int>(g_ps.build.scenes.size()) && (g_ps.build.scenes.erase(g_ps.build.scenes.begin() + num(2)), true);
    else ok = false;
    std::printf("settings: set %s %s\n", k.c_str(), ok ? "ok" : "refused");
  } else if (act == "pssave") {
    ensure();
    std::string rep;
    const bool ok = saveAll(a, rep);
    std::printf("settings: save %s %s\n", ok ? "ok" : "failed", rep.c_str());
  } else if (act == "psrevert") {
    reload(a);
    std::printf("settings: reverted\n");
  } else if (act == "psdump") {
    ensure();
    std::string tg;
    for (const std::string& t : g_ps.tags.tags) tg += (tg.empty() ? "" : ",") + t;
    std::string ly;
    for (int i = 0; i < kLayerCount; ++i) if (!g_ps.tags.layers[static_cast<size_t>(i)].empty()) ly += (ly.empty() ? "" : ",") + std::to_string(i) + "=" + g_ps.tags.layers[static_cast<size_t>(i)];
    std::printf("settings: tags=[%s] layers=[%s]\n", tg.c_str(), ly.c_str());
    int ig = static_cast<int>(g_ps.phys.ignore.size());
    std::printf("settings: physics gravity=%g,%g,%g triggers=%d ignored_pairs=%d\n", static_cast<double>(g_ps.phys.gravity[0]), static_cast<double>(g_ps.phys.gravity[1]),
                static_cast<double>(g_ps.phys.gravity[2]), g_ps.phys.queriesHitTriggers ? 1 : 0, ig);
    std::string l2;
    for (const std::string& l : g_ps.p2.layers) l2 += (l2.empty() ? "" : ",") + l;
    std::printf("settings: physics2d layers=[%s] ignored_pairs=%zu\n", l2.c_str(), g_ps.p2.ignore.size());
    std::printf("settings: time fixed=%.6f maxdelta=%g scale=%g\n", g_ps.time.fixedTimestep, g_ps.time.maximumDelta, g_ps.time.timeScale);
    std::string sc;
    for (size_t i = 0; i < g_ps.build.scenes.size(); ++i) sc += (sc.empty() ? "" : ",") + std::string(g_ps.build.scenes[i].enabled ? "" : "!") + sceneName(a, g_ps.build.scenes[i].guid);
    std::printf("settings: scenes=[%s]\n", sc.c_str());
    const ConsoleTargetData& ct = consoleTargetFor(a.project.root);
    std::printf("settings: video %ux%u refresh=%u scalar=%s depth=%s (read only)\n", ct.width, ct.height, ct.refreshHz, ct.scalar.c_str(), ct.depth.c_str());
    int bad = 0;
    for (int t = 0; t < kTabCount; ++t) for (const std::string& p : problemsOf(t)) { std::printf("settings: problem [%s] %s\n", kTabIds[t], p.c_str()); ++bad; }
    std::printf("settings: dirty tags=%d physics=%d physics2d=%d time=%d scenes=%d problems=%d\n", dirtyTab(kTags), dirtyTab(kPhysics), dirtyTab(kPhysics2D), dirtyTab(kTime), dirtyTab(kScenes), bad);
  } else {
    return false;
  }
  std::fflush(stdout);
  return true;
}

}  // namespace

RENSGA_EDITOR_PANEL(project_settings, "Project Settings", drawSettings, settingsHook, nullptr, 671, 97, ::rensga::editor::PanelDraw::OverlayEditor)

}  // namespace editor
}  // namespace rensga
