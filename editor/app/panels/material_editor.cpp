// F6.02 (Editor 8): the Material Editor. Edits a .mat.json asset with the options the target can draw: texture, color, blend mode,
// alpha test, unlit; fog is shown with the target's support. Every row the ConsoleTarget cannot honour is greyed out with its reason.
// The preview sphere is lit like the player's renderer (ambient, one directional and one point light, per vertex, texture nearest).
// Edits are written at once (atomic), one undo step each, and the lit Scene view recooks.
//
//   --ui-event verbs: matedit:<Assets/..path or GUID> | matset:<key>=<value> (texture=<guid|none>, color=r,g,b, blend=opaque|alpha,
//   alpha=<0..255>, unlit=0|1, alphatest=0|1) | matundo | matredo | matclose | matdump (prints "material: ...")
#define GL_GLEXT_PROTOTYPES 1
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app.h"
#include "panel_registry.h"
#include "panels/asset_ui.h"
#include "panels/material_editor.h"
#include "panels/tile_tools.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/material_document.h"

namespace rensga {
namespace editor {

namespace {

const char* kVs = R"(#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNrm; layout(location=2) in vec2 aUv;
uniform mat4 uVP; uniform mat4 uModel; uniform vec4 uMat; uniform int uLit;
out vec4 vCol; out vec2 vUv;
void main(){
  vec4 wp = uModel * vec4(aPos, 1.0);
  vec3 n = normalize(mat3(uModel) * aNrm);
  vec3 l = vec3(0.30);                                          // ambient
  if (uLit != 0) {
    l += vec3(1.0, 0.96, 0.88) * 0.85 * max(dot(n, normalize(vec3(-0.45, 0.70, 0.55))), 0.0);   // directional
    vec3 d = vec3(-1.6, -0.6, 1.4) - wp.xyz; float dist = length(d);
    float a = clamp(1.0 - dist / 4.5, 0.0, 1.0);
    l += vec3(0.55, 0.75, 1.0) * a * a * max(dot(n, d / max(dist, 0.001)), 0.0);                // point
  } else l = vec3(1.0);
  vCol = vec4(l * uMat.rgb, uMat.a); vUv = aUv;
  gl_Position = uVP * wp;
})";
const char* kFs = R"(#version 330 core
in vec4 vCol; in vec2 vUv; out vec4 o;
uniform sampler2D uTex; uniform int uAlphaTest;
void main(){
  vec4 t = texture(uTex, vUv);
  vec4 c = t * vCol;
  if (uAlphaTest != 0 && c.a < 0.5) discard;
  o = vec4(clamp(c.rgb, 0.0, 1.0), c.a);
})";

struct MatEd {
  bool open = false;
  MaterialDocument doc;
  std::string guid, rel, error, status;
  RenderTarget rt;
  unsigned prog = 0, vao = 0, vbo = 0, white = 0;
  int count = 0;
  float yaw = 0.6f;
};
MatEd* g_me = nullptr;  // leaked: GL objects must not be freed after the context is gone
MatEd& me() {
  if (!g_me) g_me = new MatEd();
  return *g_me;
}

unsigned build(unsigned type, const char* src) {
  unsigned s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  return s;
}

void initGl(MatEd& m) {
  if (m.prog) return;
  unsigned v = build(GL_VERTEX_SHADER, kVs), f = build(GL_FRAGMENT_SHADER, kFs);
  m.prog = glCreateProgram();
  glAttachShader(m.prog, v); glAttachShader(m.prog, f);
  glLinkProgram(m.prog);
  glDeleteShader(v); glDeleteShader(f);
  // a UV sphere: 28 x 18, position, normal, uv per vertex, as a triangle list
  std::vector<float> vb;
  const int nu = 28, nv = 18;
  auto vert = [&](int i, int j) {
    const float u = static_cast<float>(i) / nu, w = static_cast<float>(j) / nv;
    const float th = u * 6.2831853f, ph = w * 3.1415926f;
    const float x = std::sin(ph) * std::cos(th), y = std::cos(ph), z = std::sin(ph) * std::sin(th);
    const float p[8] = {x * 0.9f, y * 0.9f, z * 0.9f, x, y, z, u * 2.0f, w};
    vb.insert(vb.end(), p, p + 8);
  };
  for (int i = 0; i < nu; ++i)
    for (int j = 0; j < nv; ++j) {
      vert(i, j); vert(i + 1, j); vert(i + 1, j + 1);
      vert(i, j); vert(i + 1, j + 1); vert(i, j + 1);
    }
  m.count = static_cast<int>(vb.size() / 8);
  glGenVertexArrays(1, &m.vao); glGenBuffers(1, &m.vbo);
  glBindVertexArray(m.vao);
  glBindBuffer(GL_ARRAY_BUFFER, m.vbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<long>(vb.size() * sizeof(float)), vb.data(), GL_STATIC_DRAW);
  glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, nullptr);
  glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(12));
  glEnableVertexAttribArray(2); glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<void*>(24));
  glBindVertexArray(0);
  const unsigned char px[4] = {255, 255, 255, 255};
  glGenTextures(1, &m.white);
  glBindTexture(GL_TEXTURE_2D, m.white);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

unsigned renderPreview(App& a, MatEd& m, int size) {
  initGl(m);
  if (!m.prog) return 0;
  m.rt.ensure(size, size, 0, false);
  const MaterialData& d = m.doc.data();
  unsigned tex = m.white;
  if (!d.texture.empty()) {
    bindScene2dResources(a);
    if (const Scene2DResources::Tex* t = a.res2d.texture(d.texture)) if (t->gl) tex = t->gl;
  }
  GLint fbo = 0, vp[4];
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  glGetIntegerv(GL_VIEWPORT, vp);
  glBindFramebuffer(GL_FRAMEBUFFER, m.rt.fbo);
  glViewport(0, 0, size, size);
  const ImVec4 bg = brandToken("surface-2");
  glClearColor(bg.x, bg.y, bg.z, 1);
  glClearDepth(1.0);
  glDisable(GL_SCISSOR_TEST); glDisable(GL_CULL_FACE);
  glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(m.prog);
  // camera on +z looking at the origin; a left-handed-looking projection is not needed here: plain GL
  const float f = 1.0f / std::tan(0.5f * 40.0f * 3.14159265f / 180.0f), n = 0.1f, fa = 20.0f;
  const float proj[16] = {f, 0, 0, 0, 0, f, 0, 0, 0, 0, (fa + n) / (n - fa), -1, 0, 0, 2 * fa * n / (n - fa), 0};
  const float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -3.2f, 1};
  float vpm[16];
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += proj[k * 4 + r] * view[c * 4 + k];
      vpm[c * 4 + r] = s;
    }
  const float cy = std::cos(m.yaw), sy = std::sin(m.yaw);
  const float model[16] = {cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1};
  glUniformMatrix4fv(glGetUniformLocation(m.prog, "uVP"), 1, GL_FALSE, vpm);
  glUniformMatrix4fv(glGetUniformLocation(m.prog, "uModel"), 1, GL_FALSE, model);
  glUniform4f(glGetUniformLocation(m.prog, "uMat"), d.color[0] / 255.0f, d.color[1] / 255.0f, d.color[2] / 255.0f, d.color[3] / 255.0f);
  glUniform1i(glGetUniformLocation(m.prog, "uLit"), d.unlit ? 0 : 1);
  glUniform1i(glGetUniformLocation(m.prog, "uAlphaTest"), d.alphaTest ? 1 : 0);
  glUniform1i(glGetUniformLocation(m.prog, "uTex"), 0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  if (d.color[3] < 255) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
  glBindVertexArray(m.vao);
  glDrawArrays(GL_TRIANGLES, 0, m.count);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  return m.rt.tex;
}

void afterEdit(App& a, MatEd& m) {
  std::string err;
  if (!m.doc.save(err)) { m.error = err; return; }
  m.error.clear();
  m.status = "saved " + m.rel;
  a.editWorld.release();  // the lit Scene view cooks the scene again with the new material
}

void drawRow(const char* label) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::TableSetColumnIndex(1);
}

void drawMaterialEditor(App& a) {
  MatEd& m = me();
  if (!m.open || !a.loaded) return;
  const MaterialRules rules = materialRulesFor(consoleTargetFor(a.project.root));
  ImGui::SetNextWindowSize(ImVec2(520, 400), ImGuiCond_Appearing);
  if (!ImGui::Begin("Material Editor", &m.open)) { ImGui::End(); return; }
  ImGui::PushFont(a.fonts.uiBold, 0.0f);
  ImGui::TextUnformatted(m.doc.path().filename().string().c_str());
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", m.rel.c_str());
  if (!m.error.empty()) ImGui::TextColored(brandToken("danger"), "[x] %s", m.error.c_str());

  const MaterialData d = m.doc.data();
  ImGui::BeginGroup();
  const unsigned prev = renderPreview(a, m, 200);
  if (prev) ImGui::Image(static_cast<ImTextureID>(prev), ImVec2(200, 200), ImVec2(0, 1), ImVec2(1, 0));
  ImGui::SetNextItemWidth(200);
  ImGui::SliderAngle("##yaw", &m.yaw, -180, 180, "turn %.0f");
  ImGui::EndGroup();
  ImGui::SameLine();
  ImGui::BeginGroup();
  if (ImGui::BeginTable("##matfields", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
    ImGui::TableSetupColumn("l", ImGuiTableColumnFlags_WidthFixed, 96);
    ImGui::TableSetupColumn("w", ImGuiTableColumnFlags_WidthStretch);
    // texture
    drawRow("Texture");
    {
      const AssetEntry* te = d.texture.empty() ? nullptr : assetUi().sync.db().findGuid(d.texture);
      const std::string label = te ? te->path.filename().string() : (d.texture.empty() ? "None" : "Missing " + d.texture);
      if (ImGui::Button((label + "##tex").c_str(), ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("pick_tex");
      if (ImGui::BeginPopup("pick_tex")) {
        if (ImGui::Selectable("None", d.texture.empty()) && m.doc.setTexture("")) afterEdit(a, m);
        for (const AssetEntry& e : assetUi().sync.db().entries())
          if (e.kind == AssetKind::Texture && ImGui::Selectable(e.rel.c_str(), e.guid == d.texture) && m.doc.setTexture(e.guid)) afterEdit(a, m);
        ImGui::EndPopup();
      }
      if (te && rules.maxTextureSize) {
        int tw = 0, th = 0;
        bindScene2dResources(a);
        if (a.res2d.textureSize(te->guid, tw, th) && static_cast<uint32_t>(std::max(tw, th)) > rules.maxTextureSize) {
          ImGui::TextColored(brandToken("warn"), "[!] %d x %d is larger than the target's %u px: it is downscaled", tw, th, rules.maxTextureSize);
        }
      }
    }
    // color
    drawRow("Color");
    {
      float c[3] = {d.color[0] / 255.0f, d.color[1] / 255.0f, d.color[2] / 255.0f};
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::ColorEdit3("##color", c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_DisplayRGB) &&
          m.doc.setColor(static_cast<int>(c[0] * 255 + 0.5f), static_cast<int>(c[1] * 255 + 0.5f), static_cast<int>(c[2] * 255 + 0.5f)))
        afterEdit(a, m);
    }
    // blend mode
    drawRow("Blend mode");
    {
      int mode = d.blend() == BlendMode::Alpha ? 1 : 0;
      const char* items[] = {"Opaque", "Alpha blended"};
      ImGui::BeginDisabled(!rules.blend);
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::Combo("##blend", &mode, items, 2) && m.doc.setBlend(mode == 1 ? BlendMode::Alpha : BlendMode::Opaque, rules)) afterEdit(a, m);
      ImGui::EndDisabled();
      if (!rules.blend) ImGui::TextDisabled("%s", rules.noBlendReason.c_str());
      if (d.blend() == BlendMode::Alpha) {
        int alpha = d.color[3];
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##alpha", &alpha, 0, 254, "alpha %d") && m.doc.setAlpha(alpha, rules)) afterEdit(a, m);
      }
    }
    drawRow("Alpha test");
    {
      bool at = d.alphaTest;
      if (ImGui::Checkbox("##at", &at) && m.doc.setAlphaTest(at)) afterEdit(a, m);
      ImGui::SameLine();
      ImGui::TextDisabled("discard pixels below 50%% alpha");
    }
    drawRow("Unlit");
    {
      bool un = d.unlit;
      ImGui::BeginDisabled(!rules.lighting);
      if (ImGui::Checkbox("##unlit", &un) && m.doc.setUnlit(un, rules)) afterEdit(a, m);
      ImGui::EndDisabled();
      ImGui::SameLine();
      ImGui::TextDisabled(rules.lighting ? "vertex color x texture x color only" : "%s", rules.lighting ? "" : rules.noLightingReason.c_str());
    }
    drawRow("Fog");
    ImGui::BeginDisabled(!rules.fog);
    ImGui::TextUnformatted(rules.fog ? "from the scene's RenderSettings" : "not drawn by this target");
    ImGui::EndDisabled();
    if (!rules.fog) ImGui::TextDisabled("%s", rules.noFogReason.c_str());
    ImGui::EndTable();
  }
  ImGui::BeginDisabled(m.doc.undoDepth() == 0);
  if (ImGui::Button("Undo")) { if (m.doc.undo()) afterEdit(a, m); }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("Redo")) { if (m.doc.redo()) afterEdit(a, m); }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", m.status.c_str());
  ImGui::EndGroup();
  ImGui::End();
}

bool openMaterial(App& a, const std::string& what, std::string& err) {
  assetUi().sync.db();  // opened by the Project window
  const AssetEntry* e = assetUi().sync.db().findGuid(what);
  if (!e) e = assetUi().sync.db().findPath(what);
  if (!e || e->kind != AssetKind::Material) { err = "no material asset '" + what + "'"; return false; }
  MatEd& m = me();
  if (!m.doc.load(e->path, err)) return false;
  m.guid = e->guid;
  m.rel = e->rel;
  m.error.clear();
  m.status.clear();
  const MaterialRules rules = materialRulesFor(consoleTargetFor(a.project.root));
  if (m.doc.normalize(rules)) afterEdit(a, m);  // a material from a project with other limits: bring it inside this target's
  m.open = true;
  return true;
}

bool materialHook(App& a, const std::string& act, const std::string& arg) {
  if (act == "matedit") {
    std::string err;
    if (!openMaterial(a, arg, err)) std::printf("material: error %s\n", err.c_str());
    else std::printf("material: open %s\n", me().rel.c_str());
    return true;
  }
  if (act != "matset" && act != "matundo" && act != "matredo" && act != "matclose" && act != "matdump") return false;
  MatEd& m = me();
  if (act == "matclose") { m.open = false; return true; }
  const MaterialRules rules = materialRulesFor(consoleTargetFor(a.project.root));
  if (act == "matundo") { if (m.doc.undo()) afterEdit(a, m); }
  else if (act == "matredo") { if (m.doc.redo()) afterEdit(a, m); }
  else if (act == "matset") {
    const size_t eq = arg.find('=');
    const std::string k = arg.substr(0, eq), v = eq == std::string::npos ? "" : arg.substr(eq + 1);
    bool ok = false;
    if (k == "texture") ok = m.doc.setTexture(v == "none" ? "" : v);
    else if (k == "color") {
      int r = 0, g = 0, b = 0;
      ok = std::sscanf(v.c_str(), "%d,%d,%d", &r, &g, &b) == 3 && m.doc.setColor(r, g, b);
    } else if (k == "blend") ok = m.doc.setBlend(v == "alpha" ? BlendMode::Alpha : BlendMode::Opaque, rules);
    else if (k == "alpha") ok = m.doc.setAlpha(std::atoi(v.c_str()), rules);
    else if (k == "unlit") ok = m.doc.setUnlit(v == "1", rules);
    else if (k == "alphatest") ok = m.doc.setAlphaTest(v == "1");
    if (ok) afterEdit(a, m);
    else std::printf("material: refused %s (this target cannot draw it, or the value is bad)\n", arg.c_str());
  }
  const MaterialData& d = m.doc.data();
  std::printf("material: %s texture=%s color=%d,%d,%d,%d unlit=%d alphaTest=%d blend=%s dirty=%d undo=%zu lighting=%d fog=%d\n", m.rel.c_str(),
              d.texture.empty() ? "none" : d.texture.c_str(), d.color[0], d.color[1], d.color[2], d.color[3], d.unlit ? 1 : 0, d.alphaTest ? 1 : 0,
              d.blend() == BlendMode::Alpha ? "alpha" : "opaque", m.doc.dirty() ? 1 : 0, m.doc.undoDepth(), rules.lighting ? 1 : 0, rules.fog ? 1 : 0);
  return true;
}

}  // namespace

void openMaterialEditor(App& a, const std::string& pathOrGuid) {
  std::string err;
  if (!openMaterial(a, pathOrGuid, err)) EditorLog::instance().warn("material editor: " + err);
}

RENSGA_EDITOR_PANEL(material_editor, "Material Editor", drawMaterialEditor, materialHook, nullptr, 880, 135, ::rensga::editor::PanelDraw::OverlayEditor)

}  // namespace editor
}  // namespace rensga
