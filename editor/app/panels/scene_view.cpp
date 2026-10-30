// Scene window (MVP-48/49, F3.01): the scene rendered into an FBO, Unity-style fly camera, a 2D mode for 2D scenes,
// ID-buffer picking, rectangle selection, move/rotate/scale gizmos over the whole selection, the engine's Gizmos list
// and the orientation cube. All edits go through SceneDocument; one drag of a gizmo is one undo step.
#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app.h"
#include "panels/asset_ui.h"
#include "rensga/core/gizmos.h"
#include "rensga/editor/document_gizmos.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/scene2d.h"

namespace rensga {
namespace editor {

static void setCol(float out[4], ImVec4 c, float alpha = -1) {
  out[0] = c.x; out[1] = c.y; out[2] = c.z; out[3] = alpha < 0 ? c.w : alpha;
}

static Color gizmoColor(const char* token) {
  const ImVec4 c = brandToken(token);
  return Color(c.x, c.y, c.z, 1);
}

void App::saveCamera() {
  if (!persistLayout || !loaded || !sv.camDirty) return;
  sv.cam.save(cameraFile());
  sv.camDirty = false;
}

static void frameAll(SceneViewState& sv, const SceneDocument& doc) {
  const auto& objs = doc.scene().objects;
  if (objs.empty()) return;
  float c[3] = {0, 0, 0};
  for (const SceneObject& o : objs) for (int i = 0; i < 3; ++i) c[i] += o.position[i] / objs.size();
  float r = 2;
  for (const SceneObject& o : objs) {
    float d = 0;
    for (int i = 0; i < 3; ++i) d += (o.position[i] - c[i]) * (o.position[i] - c[i]);
    r = std::max(r, std::sqrt(d) + 1.0f);
  }
  sv.cam.frame(c, r);
  sv.camDirty = true;
}

// Bounds of the 2D content of `guids` (all objects when empty): sprites, tile layers, colliders, else the position.
static bool bounds2D(App& a, const Scene2D& s2d, const std::vector<std::string>& guids, float mn[2], float mx[2]) {
  mn[0] = mn[1] = 1e30f;
  mx[0] = mx[1] = -1e30f;
  auto add = [&](float x, float y) { mn[0] = std::min(mn[0], x); mn[1] = std::min(mn[1], y); mx[0] = std::max(mx[0], x); mx[1] = std::max(mx[1], y); };
  auto wanted = [&](const std::string& g) { return guids.empty() || std::find(guids.begin(), guids.end(), g) != guids.end(); };
  const TextureSizeFn sizeFn = [&](const std::string& g, int& w, int& h) { return a.res2d.textureSize(g, w, h); };
  bool any = false;
  for (const Sprite2D& s : s2d.sprites) {
    float c[12];
    if (!wanted(s.guid) || !spriteWorldCorners(s, sizeFn, c)) continue;
    for (int i = 0; i < 4; ++i) add(c[i * 3], c[i * 3 + 1]);
    any = true;
  }
  for (const TileLayer2D& l : s2d.layers) {
    const TilemapData* tm = a.res2d.tilemap(l.map);
    if (!wanted(l.guid) || !tm) continue;
    add(l.model[12], l.model[13]);
    add(l.model[12] + static_cast<float>(tm->width * tm->tileSize), l.model[13] + static_cast<float>(tm->height * tm->tileSize));
    any = true;
  }
  if (!any && !guids.empty()) {
    for (const std::string& g : guids) {
      double w[16];
      if (!a.doc.find(g)) continue;
      a.doc.worldMatrix(g, w);
      add(static_cast<float>(w[12]) - 16, static_cast<float>(w[13]) - 16);
      add(static_cast<float>(w[12]) + 16, static_cast<float>(w[13]) + 16);
      any = true;
    }
  }
  return any;
}

static bool toolButton(const char* label, bool on, const char* tip) {
  ImGui::PushStyleColor(ImGuiCol_Button, on ? brandToken("border-strong") : brandToken("surface"));
  ImGui::PushStyleColor(ImGuiCol_Text, on ? brandToken("text") : brandToken("text-2"));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6);
  bool r = ImGui::Button(label);
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
  if (tip && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
  return r;
}

// Names for the status line of tests.
static std::string nameOf(App& a, const std::string& guid) {
  const SceneObject* o = a.doc.find(guid);
  return o ? o->name : std::string("(none)");
}

void drawSceneView(App& a) {
  if (!a.showScene) return;
  if (a.focusScene) { ImGui::SetNextWindowFocus(); a.focusScene = false; }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  bool open = ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (!open) { ImGui::End(); return; }

  SceneViewState& sv = a.sv;
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 p0 = ImGui::GetCursorScreenPos(), sz = ImGui::GetContentRegionAvail();
  if (sz.x < 8 || sz.y < 8 || !a.renderer.ready()) { ImGui::Dummy(sz); ImGui::End(); return; }
  const ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
  const float cubeSize = 104;
  const ImVec2 cubePos(p1.x - cubeSize - 10, p0.y + 10);
  const ImVec2 ovMin(p0.x + 8, p0.y + 8), ovMax(p0.x + 470, p0.y + 40);

  // textures and tilemaps are found through the asset database
  static bool resolverSet = false;
  if (!resolverSet) {
    a.res2d.setResolver([](const std::string& g) {
      const AssetEntry* e = assetUi().sync.db().findGuid(g);
      return e ? e->path : std::filesystem::path();
    });
    resolverSet = true;
  }
  const std::string scenePath = a.doc.loaded() ? a.doc.path().string() : std::string();
  if (a.doc.loaded() && sv.modeScene != scenePath) {  // a new scene: pick 2D or 3D mode, frame it
    sv.modeScene = scenePath;
    sv.mode2d = sceneIs2D(a.doc);
    sv.needFrame2d = true;
    a.res2d.clear();
  }

  SceneDraw sd = gatherScene(a.doc, a.meshes);
  Scene2D s2d;
  if (a.doc.loaded()) s2d = gatherScene2D(a.doc);
  if (sv.needFrame && a.doc.loaded()) { frameAll(sv, a.doc); sv.needFrame = false; }
  if (sv.needFrame2d && a.doc.loaded() && sv.mode2d) {
    float mn[2], mx[2];
    if (bounds2D(a, s2d, {}, mn, mx)) sv.cam2.frame(mn[0], mn[1], mx[0], mx[1], sz.x, sz.y);
    else { sv.cam2.center[0] = sv.cam2.center[1] = 0; sv.cam2.ppu = 1; }
    sv.needFrame2d = false;
  }

  // object ids for ID-buffer picking
  std::map<std::string, unsigned> ids;
  sv.lastIds.clear();
  for (const SceneObject& o : a.doc.scene().objects) {
    sv.lastIds.push_back(o.guid);
    ids[o.guid] = static_cast<unsigned>(sv.lastIds.size());
  }
  std::vector<SpriteQuad> quads;
  if (s2d.any()) quads = a.res2d.build(s2d, ids);

  // ---- input ----
  const bool windowHot = ImGui::IsWindowHovered();
  auto inRect = [&](ImVec2 mn, ImVec2 mx) { return io.MousePos.x >= mn.x && io.MousePos.x < mx.x && io.MousePos.y >= mn.y && io.MousePos.y < mx.y; };
  const ImVec2 cubeEnd(cubePos.x + cubeSize, cubePos.y + cubeSize);
  const bool inView = windowHot && inRect(p0, p1) && !inRect(ovMin, ovMax) && (sv.mode2d || !inRect(cubePos, cubeEnd));
  const bool focusedOrHot = ImGui::IsWindowFocused() || windowHot;
  if (sv.drag == 0 && inView && !ImGuizmo::IsUsing()) {
    if (io.MouseClicked[1]) { sv.drag = 1; sv.rmbClick = true; sv.rmbPos = io.MousePos; }
    else if (io.MouseClicked[2]) sv.drag = 2;
    else if (io.MouseClicked[0] && io.KeyAlt) sv.drag = 3;
  }
  if (sv.drag == 1 && !io.MouseDown[1]) {  // a click without drag opens the create menu; a drag was fly-look / pan
    if (sv.rmbClick && a.doc.loaded()) sv.openCreateMenu = true;
    sv.rmbClick = false;
    sv.drag = 0;
  }
  if (sv.drag == 2 && !io.MouseDown[2]) sv.drag = 0;
  if (sv.drag == 3 && !io.MouseDown[0]) sv.drag = 0;
  const float dt = io.DeltaTime;
  if (sv.drag == 1) {
    const float mx = io.MousePos.x - sv.rmbPos.x, my = io.MousePos.y - sv.rmbPos.y;
    if (mx * mx + my * my > 16 || io.MouseWheel != 0) sv.rmbClick = false;
    if (sv.mode2d) {
      sv.cam2.pan(io.MouseDelta.x, io.MouseDelta.y);
    } else {
      sv.cam.look(io.MouseDelta.x, io.MouseDelta.y);
      float r = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.f : 0.f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.f : 0.f);
      float f = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.f : 0.f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.f : 0.f);
      float u = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.f : 0.f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.f : 0.f);
      if (io.MouseWheel != 0) sv.flySpeed = std::clamp(sv.flySpeed * std::pow(1.2f, io.MouseWheel), 0.5f, 400.0f);  // wheel = fly speed
      float speed = sv.flySpeed * (io.KeyShift ? 3.5f : 1.0f);
      if (r != 0 || f != 0 || u != 0) { sv.cam.fly(r, u, f, speed * dt); sv.rmbClick = false; }
    }
    sv.camDirty = true;
  } else if (sv.drag == 2 || (sv.drag == 3 && sv.mode2d)) {
    if (sv.mode2d) sv.cam2.pan(io.MouseDelta.x, io.MouseDelta.y); else sv.cam.pan(io.MouseDelta.x, io.MouseDelta.y);
    sv.camDirty = true;
  } else if (sv.drag == 3) {
    sv.cam.orbit(io.MouseDelta.x, io.MouseDelta.y);
    sv.camDirty = true;
  } else if (inView && io.MouseWheel != 0 && !ImGuizmo::IsUsing()) {
    if (sv.mode2d) sv.cam2.zoomAt(io.MouseWheel, io.MousePos.x - p0.x, io.MousePos.y - p0.y, sz.x, sz.y);
    else sv.cam.zoom(io.MouseWheel);
    sv.camDirty = true;
  }
  auto frameSelection = [&]() {
    const std::vector<std::string> sel = a.doc.selection();
    if (sv.mode2d) {
      float mn[2], mx[2];
      if (bounds2D(a, s2d, sel, mn, mx)) sv.cam2.frame(mn[0], mn[1], mx[0], mx[1], sz.x, sz.y);
      return;
    }
    float c[3] = {0, 0, 0}, r = 0;
    int n = 0;
    for (const std::string& g : sel) {
      float cc[3], rr;
      if (!objectBounds(sd, g, cc, &rr)) continue;
      for (int i = 0; i < 3; ++i) c[i] += cc[i];
      r = std::max(r, rr);
      ++n;
    }
    if (n == 0) { frameAll(sv, a.doc); return; }
    for (int i = 0; i < 3; ++i) c[i] /= static_cast<float>(n);
    float far = 0;  // radius that covers every selected object
    for (const std::string& g : sel) {
      float cc[3], rr;
      if (!objectBounds(sd, g, cc, &rr)) continue;
      float d = std::sqrt((cc[0] - c[0]) * (cc[0] - c[0]) + (cc[1] - c[1]) * (cc[1] - c[1]) + (cc[2] - c[2]) * (cc[2] - c[2]));
      far = std::max(far, d + rr);
    }
    sv.cam.frame(c, far);
    sv.camDirty = true;
  };
  if (focusedOrHot && sv.drag == 0 && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) sv.op = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) sv.op = 1;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) sv.op = 2;
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) frameSelection();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) sv.boxing = false;
  }
  if (sv.testFrame) { sv.testFrame = false; frameSelection(); }
  if (!io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2]) a.saveCamera();

  // ---- matrices, gizmo list, render ----
  const float fs = io.DisplayFramebufferScale.x;
  a.sceneRT.ensure(static_cast<int>(sz.x * fs), static_cast<int>(sz.y * fs), 4, false);
  RenderOptions ro;
  if (sv.mode2d) {
    ro.view = sv.cam2.view();
    ro.proj = sv.cam2.proj(sz.x, sz.y);
    ro.mode2d = true;
    ro.ppu = sv.cam2.ppu;
    ro.gridStep2d = sv.cam2.gridStep();
    ro.farClip = Editor2DCamera::kDepth;
  } else {
    ro.view = sv.cam.view();
    ro.proj = sv.cam.proj(sz.x / sz.y);
    sv.cam.eye(ro.eye);
    ro.farClip = sv.cam.farClip;
    ro.gridStep = std::pow(10.0f, std::floor(std::log10(std::max(sv.cam.distance, 0.5f) / 5.0f)));
  }
  sv.lastP0 = p0; sv.lastSize = sz; sv.lastView = ro.view; sv.lastProj = ro.proj;
  ro.grid = ro.markers = true;
  ro.gizmos = true;
  ro.quads = quads.empty() ? nullptr : &quads;
  ro.selected = a.doc.selected();
  ro.selection = a.doc.selection();
  setCol(ro.clear, brandToken("bg"), 1);
  setCol(ro.gridColor, brandToken("text-2"), 0.55f);
  setCol(ro.xColor, brandToken("danger"));
  setCol(ro.zColor, sv.mode2d ? brandToken("ok") : brandToken("link"));
  setCol(ro.markerColor, brandToken("text-2"));
  setCol(ro.selectColor, brandToken("primary"));

  GizmoDraw::clear();
  {
    DocumentGizmoContext gc;
    gc.style.collider = gizmoColor("ok");
    gc.style.trigger = gizmoColor("info");
    gc.style.camera = gizmoColor("text");
    gc.style.deadZone = gizmoColor("warn");
    gc.style.bounds = gizmoColor("link");
    gc.style.tilemap = gizmoColor("text-2");
    gc.style.selection = gizmoColor("primary");
    gc.style.light = gizmoColor("warn");
    gc.is2D = sv.mode2d;
    const ConsoleTargetData& ctd = consoleTargetFor(a.project.root);
    if (ctd.width > 0 && ctd.height > 0) { gc.viewW = static_cast<float>(ctd.width); gc.viewH = static_cast<float>(ctd.height); }
    gc.textureSize = [&](const std::string& g, int& w, int& h) { return a.res2d.textureSize(g, w, h); };
    gc.tilemapSize = [&](const std::string& g, int& w, int& h, int& ts) {
      const TilemapData* tm = a.res2d.tilemap(g);
      if (!tm) return false;
      w = tm->width; h = tm->height; ts = tm->tileSize;
      return true;
    };
    if (a.doc.loaded()) buildDocumentGizmos(a.doc, gc);
    // Script OnDrawGizmos output is appended to the same list here (wired in F3 stage 3).
  }
  a.renderer.render(a.sceneRT, sd, ro);
  ImGui::Image(static_cast<ImTextureID>(a.sceneRT.tex), sz, ImVec2(0, 1), ImVec2(1, 0));
  auto rayAt = [&](ImVec2 m) { return rayFromScreen(ro.view, ro.proj, (m.x - p0.x) / sz.x, (m.y - p0.y) / sz.y); };

  // ID-buffer pick of a rectangle in view points (relative to the view's top-left); nearest to the centre first
  auto pickRect = [&](float ax, float ay, float bx, float by) {
    const float k = static_cast<float>(a.sceneRT.w) / sz.x;
    const int x0 = static_cast<int>(std::floor(std::min(ax, bx) * k)), x1 = static_cast<int>(std::ceil(std::max(ax, bx) * k));
    const int y0 = static_cast<int>(std::floor(std::min(ay, by) * k)), y1 = static_cast<int>(std::ceil(std::max(ay, by) * k));
    std::vector<std::string> out;
    for (unsigned id : a.renderer.pick(sd, ro.quads, ids, ro.view, ro.proj, sv.mode2d, a.sceneRT.w, a.sceneRT.h, x0, y0, x1, y1))
      if (id >= 1 && id <= sv.lastIds.size()) out.push_back(sv.lastIds[id - 1]);
    return out;
  };
  auto pickAtMouse = [&](ImVec2 m) {
    const float x = m.x - p0.x, y = m.y - p0.y;
    std::vector<std::string> hits = pickRect(x - 2, y - 2, x + 3, y + 3);
    return hits.empty() ? std::string() : hits[0];
  };

  // a script dragged from the Project window onto an object in the view
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("RENSGA_SCRIPT")) {
      const std::string hit = pickAtMouse(io.MousePos);
      if (hit.empty()) EditorLog::instance().warn("drop a script on an object in the Scene view");
      else addScriptToObject(a, hit, static_cast<const char*>(p->Data));
    }
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(kAssetGuidPayload))
      acceptAssetDrop(a, static_cast<const char*>(p->Data), pickAtMouse(io.MousePos));  // texture: new sprite object
    ImGui::EndDragDropTarget();
  }

  // right-click (no drag): the same create menu as the Hierarchy; the object goes where the cursor ray lands
  if (sv.openCreateMenu) {
    sv.openCreateMenu = false;
    if (sv.mode2d) {
      float w[2];
      sv.cam2.screenToWorld(sv.rmbPos.x - p0.x, sv.rmbPos.y - p0.y, sz.x, sz.y, w);
      sv.ctxPoint[0] = std::round(w[0]); sv.ctxPoint[1] = std::round(w[1]); sv.ctxPoint[2] = 0;
    } else {
      Ray ray = rayAt(sv.rmbPos);
      float len = std::sqrt(ray.d[0] * ray.d[0] + ray.d[1] * ray.d[1] + ray.d[2] * ray.d[2]);
      if (len < 1e-8f) len = 1;
      float dist = 5.0f, t = 0;
      if (!pickObject(sd, ray, &t).empty()) dist = t * len;
      else if (ray.d[1] * ray.o[1] < 0) dist = (-ray.o[1] / ray.d[1]) * len;  // ground plane y = 0
      for (int i = 0; i < 3; ++i) sv.ctxPoint[i] = ray.o[i] + ray.d[i] / len * dist;
    }
    ImGui::OpenPopup("scene_ctx");
  }
  if (ImGui::BeginPopup("scene_ctx")) {
    drawCreateMenuItems(a, "", sv.ctxPoint);
    ImGui::EndPopup();
  }

  // ---- overlay toolbar ----
  ImGui::SetCursorScreenPos(ovMin);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
  if (toolButton("Move", sv.op == 0, "Translate (W)")) sv.op = 0;
  ImGui::SameLine();
  if (toolButton("Rotate", sv.op == 1, "Rotate (E)")) sv.op = 1;
  ImGui::SameLine();
  if (toolButton("Scale", sv.op == 2, "Scale (R)")) sv.op = 2;
  ImGui::SameLine(0, 14);
  if (toolButton(sv.local ? "Local" : "World", false, "Gizmo space")) sv.local = !sv.local;
  ImGui::SameLine(0, 14);
  if (toolButton("Frame", false, "Frame selection (F)")) frameSelection();
  ImGui::SameLine(0, 14);
  if (toolButton("2D", sv.mode2d, "2D mode: orthographic, +Y down (pixel space of the 2D layer)")) {
    sv.mode2d = !sv.mode2d;
    if (sv.mode2d) sv.needFrame2d = true;
  }
  ImGui::PopStyleVar();

  // ---- gizmo over the whole selection ----
  ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
  ImGuizmo::SetRect(p0.x, p0.y, sz.x, sz.y);
  ImGuizmo::SetOrthographic(sv.mode2d);
  ImGuizmo::Enable(sv.drag == 0 && !sv.boxing);
  M4 vG = viewToRightHanded(ro.view), pG = projToRightHanded(ro.proj);
  std::vector<std::string> tops;
  for (const std::string& g : a.doc.topLevel(a.doc.selection())) {
    const SceneObject* o = a.doc.find(g);
    if (o && o->active) tops.push_back(g);
  }
  bool overGizmo = false;
  if (!tops.empty() && !a.doc.playing()) {
    // pivot: the object's own matrix for one object, else the centre of the objects with no rotation
    M4 pivot = m4Identity();
    if (tops.size() == 1) {
      double wd[16];
      a.doc.worldMatrix(tops[0], wd);
      pivot = m4FromDouble(wd);
    } else {
      float c[3] = {0, 0, 0};
      for (const std::string& g : tops) {
        double wd[16];
        a.doc.worldMatrix(g, wd);
        for (int i = 0; i < 3; ++i) c[i] += static_cast<float>(wd[12 + i]) / static_cast<float>(tops.size());
      }
      pivot[12] = c[0]; pivot[13] = c[1]; pivot[14] = c[2];
    }
    const M4 before = pivot;
    ImGuizmo::OPERATION op;
    if (sv.mode2d) op = sv.op == 0 ? (ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y) : sv.op == 1 ? ImGuizmo::ROTATE_Z : (ImGuizmo::SCALE_X | ImGuizmo::SCALE_Y);
    else op = sv.op == 0 ? ImGuizmo::TRANSLATE : sv.op == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
    ImGuizmo::MODE mode = (sv.local || sv.op == 2) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    float snap[3] = {1, 1, 1};
    if (sv.op == 0) { snap[0] = snap[1] = snap[2] = sv.mode2d ? sv.cam2.gridStep() : ro.gridStep; }
    else if (sv.op == 1) { snap[0] = snap[1] = snap[2] = 15; }
    else { snap[0] = snap[1] = snap[2] = 0.1f; }
    bool changed = ImGuizmo::Manipulate(vG.data(), pG.data(), op, mode, pivot.data(), nullptr, io.KeyCtrl ? snap : nullptr);
    bool usingNow = ImGuizmo::IsUsing();
    if (usingNow && !sv.gizmoUsing) a.doc.beginGesture();
    if (changed) {
      const M4 delta = m4Mul(pivot, m4Invert(before));  // what the gizmo did this frame, applied to every top-level object
      for (const std::string& g : tops) {
        double wd[16], d[16];
        a.doc.worldMatrix(g, wd);
        const M4 nw = m4Mul(delta, m4FromDouble(wd));
        for (int i = 0; i < 16; ++i) d[i] = nw[i];
        a.doc.setWorldMatrix(g, d);
      }
    }
    if (!usingNow && sv.gizmoUsing) a.doc.endGesture();
    sv.gizmoUsing = usingNow;
    overGizmo = ImGuizmo::IsOver() || usingNow;
  } else if (sv.gizmoUsing) {
    a.doc.endGesture();
    sv.gizmoUsing = false;
  }

  // ---- orientation cube (3D) / zoom label (2D) ----
  bool cubeHot = false;
  if (!sv.mode2d) {
    M4 before = vG;
    ImGuizmo::ViewManipulate(vG.data(), sv.cam.distance, cubePos, ImVec2(cubeSize, cubeSize), 0x30000000);
    float fb[3] = {-before[2], -before[6], -before[10]}, fa[3] = {-vG[2], -vG[6], -vG[10]};
    if (std::fabs(fb[0] - fa[0]) + std::fabs(fb[1] - fa[1]) + std::fabs(fb[2] - fa[2]) > 1e-5f) {
      sv.cam.setDirection(fa);
      sv.camDirty = true;
    }
    cubeHot = ImGuizmo::IsViewManipulateHovered() || ImGuizmo::IsUsingViewManipulate();
  } else {
    char zl[64];
    std::snprintf(zl, sizeof zl, "2D  %d%%  grid %g", static_cast<int>(sv.cam2.ppu * 100 + 0.5f), static_cast<double>(sv.cam2.gridStep()));
    const ImVec2 ts = ImGui::CalcTextSize(zl);
    ImGui::GetWindowDrawList()->AddText(ImVec2(p1.x - ts.x - 12, p0.y + 12), ImGui::GetColorU32(brandToken("text-2")), zl);
  }

  // ---- picking: click selects through the ID buffer, a drag selects a rectangle ----
  auto applyHits = [&](const std::vector<std::string>& hits, bool box) {
    const bool shift = io.KeyShift, ctrl = io.KeyCtrl;
    if (!box) {
      const std::string g = hits.empty() ? std::string() : hits[0];
      sv.lastPickName = g.empty() ? std::string() : nameOf(a, g);
      if (g.empty()) { if (!shift && !ctrl) a.doc.select(""); }
      else if (ctrl) a.doc.toggleSelect(g);
      else if (shift) a.doc.addToSelection(g);
      else a.doc.select(g);
    } else {
      std::vector<std::string> list;
      if (shift || ctrl) list = a.doc.selection();
      for (auto it = hits.rbegin(); it != hits.rend(); ++it)
        if (std::find(list.begin(), list.end(), *it) == list.end()) list.push_back(*it);
      a.doc.setSelection(list);
    }
  };
  if (inView && io.MouseClicked[0] && !io.KeyAlt) {
    sv.clickValid = !overGizmo && !cubeHot;
    sv.clickPos = io.MousePos;
    sv.boxing = false;
  }
  if (sv.clickValid && io.MouseDown[0] && !sv.boxing) {
    const float dx = io.MousePos.x - sv.clickPos.x, dy = io.MousePos.y - sv.clickPos.y;
    if (dx * dx + dy * dy >= 25) sv.boxing = true;
  }
  if (sv.boxing && sv.clickValid) {
    const ImVec2 q(std::clamp(io.MousePos.x, p0.x, p1.x), std::clamp(io.MousePos.y, p0.y, p1.y));
    const ImU32 line = ImGui::GetColorU32(brandToken("primary"));
    ImVec4 fill = brandToken("primary");
    fill.w = 0.16f;
    ImGui::GetWindowDrawList()->AddRectFilled(sv.clickPos, q, ImGui::GetColorU32(fill));
    ImGui::GetWindowDrawList()->AddRect(sv.clickPos, q, line);
  }
  if (io.MouseReleased[0] && sv.clickValid) {
    sv.clickValid = false;
    if (sv.boxing) {
      sv.boxing = false;
      const ImVec2 q(std::clamp(io.MousePos.x, p0.x, p1.x), std::clamp(io.MousePos.y, p0.y, p1.y));
      applyHits(pickRect(sv.clickPos.x - p0.x, sv.clickPos.y - p0.y, q.x - p0.x, q.y - p0.y), true);
    } else if (inView && !cubeHot && !overGizmo) {
      const std::string hit = pickAtMouse(io.MousePos);
      applyHits(hit.empty() ? std::vector<std::string>() : std::vector<std::string>{hit}, false);
    }
  }

  // ---- test verbs (--ui-event pickobj:<name>, boxsel:x0,y0,x1,y1) ----
  if (!sv.testPickName.empty()) {
    const std::string want = sv.testPickName;
    sv.testPickName.clear();
    float sx = -1, sy = -1;
    for (const SceneObject& o : a.doc.scene().objects) {
      if (o.name != want) continue;
      float c[3] = {0, 0, 0};
      int n = 0;
      for (const SpriteQuad& q : quads) {  // 2D content: centre of its quads' bounds
        if (q.id != ids[o.guid]) continue;
        for (int k = 0; k < 4; ++k) for (int i = 0; i < 3; ++i) c[i] += q.p[k][i];
        n += 4;
        break;  // first quad (a tile layer has many)
      }
      if (n) { for (float& v : c) v /= static_cast<float>(n); }
      else { double w[16]; a.doc.worldMatrix(o.guid, w); c[0] = static_cast<float>(w[12]); c[1] = static_cast<float>(w[13]); c[2] = static_cast<float>(w[14]); }
      float clip[3];
      m4Point(m4Mul(ro.proj, ro.view), c, clip);
      sx = (clip[0] * 0.5f + 0.5f) * sz.x;
      sy = (0.5f - clip[1] * 0.5f) * sz.y;
      break;
    }
    std::string got = "(off screen)";
    if (sx >= 0 && sy >= 0 && sx < sz.x && sy < sz.y) {
      auto hits = pickRect(sx - 2, sy - 2, sx + 3, sy + 3);
      got = hits.empty() ? "(nothing)" : nameOf(a, hits[0]);
      sv.lastPickName = got;
    }
    std::printf("scene: pickobj %s -> %s\n", want.c_str(), got.c_str());
    std::fflush(stdout);
  }
  if (sv.testBox) {
    sv.testBox = false;
    auto hits = pickRect(sv.testRect[0] - p0.x, sv.testRect[1] - p0.y, sv.testRect[2] - p0.x, sv.testRect[3] - p0.y);
    std::vector<std::string> list = sv.testBoxAdd ? a.doc.selection() : std::vector<std::string>();
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
      if (std::find(list.begin(), list.end(), *it) == list.end()) list.push_back(*it);
    a.doc.setSelection(list);
    std::printf("scene: boxsel -> %zu object(s)\n", a.doc.selection().size());
    std::fflush(stdout);
  }
  ImGui::End();
}

// ---- --ui-event verbs for scene editing (documented in AGENTS.md) ----
static std::string guidByName(App& a, const std::string& name) {
  for (const SceneObject& o : a.doc.scene().objects) if (o.name == name) return o.guid;
  return std::string();
}

static std::vector<std::string> splitStr(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t at = 0;
  while (true) {
    size_t e = s.find(sep, at);
    out.push_back(s.substr(at, e == std::string::npos ? std::string::npos : e - at));
    if (e == std::string::npos) break;
    at = e + 1;
  }
  return out;
}

static void dumpTree(App& a, const std::string& parent, int depth, std::string& out) {
  for (const std::string& g : a.doc.childrenOf(parent)) {
    out += std::string(static_cast<size_t>(depth) * 2 + 2, ' ') + nameOf(a, g) + (a.doc.isSelected(g) ? " *" : "") + "\n";
    dumpTree(a, g, depth + 1, out);
  }
}

bool sceneUiHook(App& a, const std::string& act, const std::string& arg) {
  auto names = [&](const std::string& list) {
    std::vector<std::string> g;
    for (const std::string& n : splitStr(list, ',')) { std::string id = guidByName(a, n); if (!id.empty()) g.push_back(id); }
    return g;
  };
  if (act == "new") {  // new:<name>[|<parent name>]: one undo step, selects it
    std::vector<std::string> p = splitStr(arg, '|');
    const std::string parent = p.size() > 1 ? guidByName(a, p[1]) : std::string();
    const std::string g = a.doc.createObject(p[0], parent);
    if (!g.empty()) a.doc.select(g);
  } else if (act == "selectmany") {
    a.doc.setSelection(names(arg));
  } else if (act == "selectadd") {
    for (const std::string& g : names(arg)) a.doc.addToSelection(g);
  } else if (act == "reparent") {  // reparent:<a,b>|<parent name or empty>[|<before name>]
    std::vector<std::string> p = splitStr(arg, '|');
    if (p.size() < 2) return true;
    const bool ok = a.doc.reparentMany(names(p[0]), p[1].empty() ? std::string() : guidByName(a, p[1]),
                                       p.size() > 2 ? guidByName(a, p[2]) : std::string());
    std::printf("scene: reparent %s\n", ok ? "ok" : "refused");
  } else if (act == "dup") {
    duplicateSelected(a);
  } else if (act == "del") {
    deleteSelected(a);
  } else if (act == "copy") {
    copySelected(a);
  } else if (act == "paste") {
    pasteSelected(a);
  } else if (act == "undo") {
    a.doc.undo();
  } else if (act == "redo") {
    a.doc.redo();
  } else if (act == "setpos") {  // setpos:<name>|x,y,z
    std::vector<std::string> p = splitStr(arg, '|');
    float v[3] = {0, 0, 0};
    const std::string g = guidByName(a, p[0]);
    if (p.size() > 1 && !g.empty() && std::sscanf(p[1].c_str(), "%f,%f,%f", &v[0], &v[1], &v[2]) == 3) {
      const SceneObject* o = a.doc.find(g);
      a.doc.setTransform(g, v, o->rotation, o->scale);
    }
  } else if (act == "reload") {  // reopen the scene file from disk (drops the undo history)
    std::string err;
    const std::filesystem::path f = a.doc.path();
    if (!a.doc.load(f, err)) std::printf("scene: reload failed: %s\n", err.c_str());
  } else if (act == "savecopy") {  // savecopy:<file>: copy of the scene file on disk (after `save`)
    std::error_code ec;
    std::filesystem::copy_file(a.doc.path(), arg, std::filesystem::copy_options::overwrite_existing, ec);
    std::printf("scene: savecopy %s\n", ec ? ec.message().c_str() : "ok");
  } else if (act == "mode") {
    a.sv.mode2d = arg == "2d";
    a.sv.modeScene = a.doc.path().string();
    a.sv.needFrame2d = a.sv.mode2d;
  } else if (act == "framesel") {
    a.sv.testFrame = true;
  } else if (act == "pickobj") {
    a.sv.testPickName = arg;
  } else if (act == "boxsel") {  // boxsel:x0,y0,x1,y1[,add] in window pixels
    float r[4];
    if (std::sscanf(arg.c_str(), "%f,%f,%f,%f", &r[0], &r[1], &r[2], &r[3]) == 4) {
      for (int i = 0; i < 4; ++i) a.sv.testRect[i] = r[i];
      a.sv.testBoxAdd = arg.find(",add") != std::string::npos;
      a.sv.testBox = true;
    }
  } else if (act == "dumpsel") {
    std::string out;
    for (const std::string& g : a.doc.selection()) out += (out.empty() ? "" : ",") + nameOf(a, g);
    std::printf("scene: selection=[%s] primary=%s picked=%s\n", out.c_str(), nameOf(a, a.doc.selected()).c_str(), a.sv.lastPickName.c_str());
  } else if (act == "dumptree") {
    std::string out;
    dumpTree(a, "", 0, out);
    std::printf("scene: tree\n%s", out.c_str());
  } else if (act == "gizmos") {
    std::printf("scene: gizmos lines=%u triangles=%u dropped=%u mode=%s\n", GizmoDraw::lineCount(), GizmoDraw::triangleCount(),
                GizmoDraw::dropped(), a.sv.mode2d ? "2d" : "3d");
  } else {
    return false;
  }
  std::fflush(stdout);
  return true;
}

}  // namespace editor
}  // namespace rensga
