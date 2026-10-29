// Scene window (MVP-48/49): the scene rendered into an FBO, Unity-style camera, click picking, transform gizmos,
// orientation cube. All edits go through SceneDocument; one drag of a gizmo is one undo step.
#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>

#include "app.h"

namespace rensga {
namespace editor {

static void setCol(float out[4], ImVec4 c, float alpha = -1) {
  out[0] = c.x; out[1] = c.y; out[2] = c.z; out[3] = alpha < 0 ? c.w : alpha;
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
  const ImVec2 ovMin(p0.x + 8, p0.y + 8), ovMax(p0.x + 360, p0.y + 40);

  SceneDraw sd = gatherScene(a.doc, a.meshes);
  if (sv.needFrame && a.doc.loaded()) { frameAll(sv, a.doc); sv.needFrame = false; }

  // ---- input ----
  const bool windowHot = ImGui::IsWindowHovered();
  auto inRect = [&](ImVec2 mn, ImVec2 mx) { return io.MousePos.x >= mn.x && io.MousePos.x < mx.x && io.MousePos.y >= mn.y && io.MousePos.y < mx.y; };
  const bool inView = windowHot && inRect(p0, p1) && !inRect(ovMin, ovMax) && !inRect(cubePos, ImVec2(cubePos.x + cubeSize, cubePos.y + cubeSize));
  const bool focusedOrHot = ImGui::IsWindowFocused() || windowHot;
  if (sv.drag == 0 && inView && !ImGuizmo::IsUsing()) {
    if (io.MouseClicked[1]) sv.drag = 1;
    else if (io.MouseClicked[2]) sv.drag = 2;
    else if (io.MouseClicked[0] && io.KeyAlt) sv.drag = 3;
  }
  if (sv.drag == 1 && !io.MouseDown[1]) sv.drag = 0;
  if (sv.drag == 2 && !io.MouseDown[2]) sv.drag = 0;
  if (sv.drag == 3 && !io.MouseDown[0]) sv.drag = 0;
  const float dt = io.DeltaTime;
  if (sv.drag == 1) {
    sv.cam.look(io.MouseDelta.x, io.MouseDelta.y);
    float r = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.f : 0.f) - (ImGui::IsKeyDown(ImGuiKey_A) ? 1.f : 0.f);
    float f = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.f : 0.f) - (ImGui::IsKeyDown(ImGuiKey_S) ? 1.f : 0.f);
    float u = (ImGui::IsKeyDown(ImGuiKey_E) ? 1.f : 0.f) - (ImGui::IsKeyDown(ImGuiKey_Q) ? 1.f : 0.f);
    float speed = 8.0f * (io.KeyShift ? 3.5f : 1.0f);
    if (r != 0 || f != 0 || u != 0) sv.cam.fly(r, u, f, speed * dt);
    sv.camDirty = true;
  } else if (sv.drag == 2) {
    sv.cam.pan(io.MouseDelta.x, io.MouseDelta.y);
    sv.camDirty = true;
  } else if (sv.drag == 3) {
    sv.cam.orbit(io.MouseDelta.x, io.MouseDelta.y);
    sv.camDirty = true;
  } else if (inView && io.MouseWheel != 0 && !ImGuizmo::IsUsing()) {
    sv.cam.zoom(io.MouseWheel);
    sv.camDirty = true;
  }
  if (focusedOrHot && sv.drag == 0 && !io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) sv.op = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) sv.op = 1;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) sv.op = 2;
    if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
      float c[3], r;
      if (!a.doc.selected().empty() && objectBounds(sd, a.doc.selected(), c, &r)) { sv.cam.frame(c, r); sv.camDirty = true; }
      else frameAll(sv, a.doc);
    }
  }
  if (!io.MouseDown[0] && !io.MouseDown[1] && !io.MouseDown[2]) a.saveCamera();

  // ---- render ----
  const float fs = io.DisplayFramebufferScale.x;
  a.sceneRT.ensure(static_cast<int>(sz.x * fs), static_cast<int>(sz.y * fs), 4, false);
  RenderOptions ro;
  ro.view = sv.cam.view();
  ro.proj = sv.cam.proj(sz.x / sz.y);
  sv.cam.eye(ro.eye);
  ro.farClip = sv.cam.farClip;
  ro.grid = ro.markers = true;
  ro.selected = a.doc.selected();
  setCol(ro.clear, brandToken("bg"), 1);
  setCol(ro.gridColor, brandToken("text-2"), 0.55f);
  setCol(ro.xColor, brandToken("danger"));
  setCol(ro.zColor, brandToken("link"));
  setCol(ro.markerColor, brandToken("text-2"));
  setCol(ro.selectColor, brandToken("primary"));
  a.renderer.render(a.sceneRT, sd, ro);
  ImGui::Image(static_cast<ImTextureID>(a.sceneRT.tex), sz, ImVec2(0, 1), ImVec2(1, 0));

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
  if (toolButton("Frame", false, "Frame selection (F)")) {
    float c[3], r;
    if (!a.doc.selected().empty() && objectBounds(sd, a.doc.selected(), c, &r)) { sv.cam.frame(c, r); sv.camDirty = true; }
    else frameAll(sv, a.doc);
  }
  ImGui::PopStyleVar();

  // ---- gizmo ----
  ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
  ImGuizmo::SetRect(p0.x, p0.y, sz.x, sz.y);
  ImGuizmo::SetOrthographic(false);
  ImGuizmo::Enable(sv.drag == 0);
  M4 vG = viewToRightHanded(ro.view), pG = projToRightHanded(ro.proj);
  const SceneObject* sel = a.doc.selected().empty() ? nullptr : a.doc.find(a.doc.selected());
  bool overGizmo = false;
  if (sel && sel->active) {
    double wd[16];
    a.doc.worldMatrix(sel->guid, wd);
    M4 model = m4FromDouble(wd);
    ImGuizmo::OPERATION op = sv.op == 0 ? ImGuizmo::TRANSLATE : sv.op == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
    ImGuizmo::MODE mode = (sv.local || sv.op == 2) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    bool changed = ImGuizmo::Manipulate(vG.data(), pG.data(), op, mode, model.data());
    bool usingNow = ImGuizmo::IsUsing();
    if (usingNow && !sv.gizmoUsing) a.doc.beginGesture();
    if (changed) {
      double d[16];
      for (int i = 0; i < 16; ++i) d[i] = model[i];
      a.doc.setWorldMatrix(sel->guid, d);
    }
    if (!usingNow && sv.gizmoUsing) a.doc.endGesture();
    sv.gizmoUsing = usingNow;
    overGizmo = ImGuizmo::IsOver() || usingNow;
  } else if (sv.gizmoUsing) {
    a.doc.endGesture();
    sv.gizmoUsing = false;
  }

  // ---- orientation cube ----
  {
    M4 before = vG;
    ImGuizmo::ViewManipulate(vG.data(), sv.cam.distance, cubePos, ImVec2(cubeSize, cubeSize), 0x30000000);
    float fb[3] = {-before[2], -before[6], -before[10]}, fa[3] = {-vG[2], -vG[6], -vG[10]};
    if (std::fabs(fb[0] - fa[0]) + std::fabs(fb[1] - fa[1]) + std::fabs(fb[2] - fa[2]) > 1e-5f) {
      sv.cam.setDirection(fa);
      sv.camDirty = true;
    }
  }

  // ---- picking: a click (not a drag) on empty gizmo space selects the nearest object ----
  const bool cubeHot = ImGuizmo::IsViewManipulateHovered() || ImGuizmo::IsUsingViewManipulate();
  if (inView && io.MouseClicked[0] && !io.KeyAlt) {
    sv.clickValid = !overGizmo && !cubeHot;
    sv.clickPos = io.MousePos;
  }
  if (io.MouseReleased[0] && sv.clickValid) {
    sv.clickValid = false;
    float dx = io.MousePos.x - sv.clickPos.x, dy = io.MousePos.y - sv.clickPos.y;
    if (dx * dx + dy * dy < 16 && inView && !cubeHot && !overGizmo) {
      Ray ray = rayFromScreen(ro.view, ro.proj, (io.MousePos.x - p0.x) / sz.x, (io.MousePos.y - p0.y) / sz.y);
      a.doc.select(pickObject(sd, ray));
    }
  }
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
