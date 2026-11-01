// F6.02 (Editor 8): the Lighting window and the Bake button. Baked vertex lighting happens in the cooker (vertex_bake.h): the meshes
// whose MeshRenderer.bakeLighting is on get the scene's lights and a cheap ambient occlusion in their vertex colors. This window
// flags the meshes (one undo step), cooks the scene on a worker thread for the open project's target with a progress line, and
// the lit Scene view then recooks (the bake results are cached) and shows the baked colors.
//
//   --ui-event verbs: lighting[:off] | bakeall (flag every static mesh, then bake) | bake (bake what is flagged) | bakeclear (unflag all)
//   | bakewait (hold the queue until the job ends) | bakedump (prints "bake: ...")
#include <cfloat>
#include <chrono>
#include <cstdio>
#include <future>

#include "app.h"
#include "panel_registry.h"
#include "rensga/cooker/cooker.h"
#include "rensga/cooker/vertex_bake_stats.h"
#include "rensga/editor/editor_world.h"
#include "rensga/editor/inspector_model.h"

namespace rensga {
namespace editor {

namespace {

struct BakeJob {
  bool open = false;
  struct Result { bool ok = false; cooker::BakeStats stats; };
  std::future<Result> fut;
  bool running = false, ready = false;
  std::string error, line;
  std::chrono::steady_clock::time_point started;
  double seconds = 0;
  int lastMeshes = 0;
};
BakeJob* g_job = nullptr;  // leaked: a future's destructor would wait on a thread at exit
BakeJob& job() {
  if (!g_job) g_job = new BakeJob();
  return *g_job;
}

struct SceneCounts { int lightsDir = 0, lightsPoint = 0, meshes = 0, flagged = 0; };
SceneCounts countScene(App& a) {
  SceneCounts c;
  for (const SceneObject& o : a.doc.scene().objects) {
    if (!o.active) continue;
    for (const SceneComponent& comp : o.components) {
      if (comp.type == "Light") (comp.fields.value("type", 0) == 1 ? c.lightsPoint : c.lightsDir)++;
      else if (comp.type == "DirectionalLight") c.lightsDir++;
      else if (comp.type == "MeshRenderer" && comp.fields.contains("mesh") && comp.fields["mesh"].is_string() && !comp.fields["mesh"].get<std::string>().empty()) {
        c.meshes++;
        if (comp.fields.value("bakeLighting", false)) c.flagged++;
      }
    }
  }
  return c;
}

// Sets bakeLighting on (or off) for every MeshRenderer with a mesh: one undo step. The targets are collected first: an edit replaces the
// scene's object list, so no reference into it may live across one.
int flagAll(App& a, bool on) {
  std::vector<ComponentRef> targets;
  for (const SceneObject& o : a.doc.scene().objects) {
    for (size_t i = 0; i < o.components.size(); ++i) {
      const SceneComponent& comp = o.components[i];
      if (comp.type != "MeshRenderer" || !comp.fields.contains("mesh") || !comp.fields["mesh"].is_string() || comp.fields["mesh"].get<std::string>().empty()) continue;
      if (comp.fields.value("bakeLighting", false) == on) continue;
      ComponentRef r;
      r.guid = o.guid;
      r.index = static_cast<int>(i);
      targets.push_back(r);
    }
  }
  a.doc.beginGesture();
  for (const ComponentRef& r : targets) editFields(a.doc, {r}, "bakeLighting", on);
  a.doc.endGesture();
  return static_cast<int>(targets.size());
}

void startBake(App& a) {
  BakeJob& j = job();
  if (j.running || !a.loaded || !a.doc.loaded()) return;
  j.error.clear();
  j.ready = false;
  j.running = true;
  j.started = std::chrono::steady_clock::now();
  cooker::bakeProgressBegin(0);
  const std::filesystem::path root = a.project.root, scene = a.doc.path();
  const std::string target = a.target.empty() ? a.project.target : a.target;
  const nlohmann::ordered_json docJson = a.doc.document();
  j.fut = std::async(std::launch::async, [root, scene, target, docJson]() {
    // a private output folder: the Inspector's validation cook and this one never write the same files
    std::error_code ec;
    CookOptions opt;
    opt.project = std::filesystem::absolute(root);
    opt.target = target;
    opt.outDir = opt.project / "Library" / "Cooked" / "editor" / (target + "-bake");
    opt.sceneOverrides[std::filesystem::relative(std::filesystem::absolute(scene), opt.project, ec).generic_string()] = docJson.dump();
    opt.budgetsAreWarnings = true;
    opt.force = true;
    BakeJob::Result r;
    r.ok = cookProject(opt, nullptr).ok;
    r.stats = cooker::lastBakeStatsThisThread();
    return r;
  });
  a.editWorld.setDebounce(1e9);  // no second cook of the same scene while the worker bakes (the Scene view keeps its last picture)
}

void poll(App& a) {
  BakeJob& j = job();
  if (!j.running) return;
  if (j.fut.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  const BakeJob::Result res = j.fut.get();
  const bool ok = res.ok;
  a.editWorld.setDebounce(0.25);
  j.running = false;
  j.ready = true;
  j.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - j.started).count();
  const cooker::BakeStats st = res.stats;
  char t[240];
  if (!ok) {
    j.error = "the scene does not cook for this target";
    j.line = j.error;
  } else {
    std::snprintf(t, sizeof t, "Baked %llu meshes, %llu vertices, %llu rays (%llu from cache) in %.1f s",
                  static_cast<unsigned long long>(st.meshes), static_cast<unsigned long long>(st.vertices), static_cast<unsigned long long>(st.rays),
                  static_cast<unsigned long long>(st.cacheHits), j.seconds);
    j.line = t;
    j.lastMeshes = static_cast<int>(st.meshes);
  }
  a.editWorld.release();  // the Scene view cooks the scene again; the bake is in the cache now
}

void drawLighting(App& a) {
  BakeJob& j = job();
  poll(a);
  if (!j.open || !a.loaded) return;
  ImGui::SetNextWindowSize(ImVec2(440, 300), ImGuiCond_Appearing);
  if (!ImGui::Begin("Lighting", &j.open)) { ImGui::End(); return; }
  const SceneCounts c = countScene(a);
  const PreviewRules pr = effectivePreviewRules(a.targetModule(), consoleTargetFor(a.project.root));
  ImGui::SeparatorText("Scene");
  ImGui::Text("%d directional and %d point lights", c.lightsDir, c.lightsPoint);
  if (pr.maxLightsPerObject > 0) {
    ImGui::TextDisabled("The target lights each object with at most %d lights.", pr.maxLightsPerObject);
    if (c.lightsDir + c.lightsPoint > pr.maxLightsPerObject)
      ImGui::TextColored(brandToken("warn"), "[!] %d lights, only %d reach one object: bake the meshes to use them all", c.lightsDir + c.lightsPoint, pr.maxLightsPerObject);
  }
  ImGui::Text("%d static meshes, %d baked (MeshRenderer > Bake lighting)", c.meshes, c.flagged);
  ImGui::SeparatorText("Bake");
  ImGui::TextWrapped("Bakes every light and ambient occlusion into the vertex colors of the flagged meshes when the scene is cooked. "
                     "A baked mesh with a material is drawn unlit: its light is in the colors.");
  ImGui::BeginDisabled(j.running || c.meshes == 0);
  if (ImGui::Button("Bake all static meshes")) { flagAll(a, true); startBake(a); }
  ImGui::SameLine();
  ImGui::BeginDisabled(c.flagged == 0);
  if (ImGui::Button("Bake again")) startBake(a);
  ImGui::SameLine();
  if (ImGui::Button("Clear baking")) { flagAll(a, false); a.editWorld.release(); }
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  if (j.running) {
    const uint64_t planned = cooker::bakeVerticesPlanned(), done = cooker::bakeVerticesDone();
    const float frac = planned ? static_cast<float>(done) / static_cast<float>(planned) : 0.0f;
    char t[120];
    const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - j.started).count();
    std::snprintf(t, sizeof t, "Baking... %llu / %llu vertices  (%.1f s)", static_cast<unsigned long long>(done), static_cast<unsigned long long>(planned), el);
    ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 0), t);
  } else if (!j.line.empty()) {
    ImGui::TextColored(j.error.empty() ? brandToken("ok") : brandToken("danger"), "%s %s", j.error.empty() ? "[ok]" : "[x]", j.line.c_str());
  }
  ImGui::End();
}

bool lightingHook(App& a, const std::string& act, const std::string& arg) {
  BakeJob& j = job();
  if (act == "lighting") { j.open = arg != "off"; std::printf("lighting: window %s\n", j.open ? "open" : "closed"); return true; }
  if (act == "bakeall") { const int n = flagAll(a, true); startBake(a); std::printf("bake: flagged %d meshes, started\n", n); return true; }
  if (act == "bake") { startBake(a); std::printf("bake: started\n"); return true; }
  if (act == "bakeclear") { const int n = flagAll(a, false); a.editWorld.release(); std::printf("bake: cleared %d meshes\n", n); return true; }
  if (act == "bakedump") {
    const SceneCounts c = countScene(a);
    const cooker::BakeStats st = cooker::lastBakeStats();
    std::printf("bake: running=%d meshes=%d flagged=%d lights=%d meshesBaked=%llu vertices=%llu rays=%llu cacheHits=%llu seconds=%.2f error=%s\n", j.running ? 1 : 0,
                c.meshes, c.flagged, c.lightsDir + c.lightsPoint, static_cast<unsigned long long>(st.meshes), static_cast<unsigned long long>(st.vertices),
                static_cast<unsigned long long>(st.rays), static_cast<unsigned long long>(st.cacheHits), j.seconds, j.error.c_str());
    return true;
  }
  return false;
}

bool lightingHolds(App&, const std::string& event) { return event.rfind("bakewait", 0) == 0 && job().running; }

}  // namespace

RENSGA_EDITOR_PANEL(lighting_panel, "Lighting", drawLighting, lightingHook, lightingHolds, 890, 136, ::rensga::editor::PanelDraw::OverlayEditor)

}  // namespace editor
}  // namespace rensga
