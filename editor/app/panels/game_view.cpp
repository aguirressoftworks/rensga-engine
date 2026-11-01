// Game window. Edit mode (MVP-50): the scene seen through its Camera component at the active target's preview resolution.
// Play mode (F3.04): the running World drawn with the desktop player's renderer (GlRenderer + renderWorld: 3D meshes and the
// 2D batch) into an offscreen target at the ConsoleTarget resolution, letterboxed (optional integer scale), with a stats overlay and
// live budget warnings. Resolution comes from TargetModule::previewRules() or, without a TargetModule, from the ConsoleTarget
// data itself; nothing here names a console.
#define GL_GLEXT_PROTOTYPES 1
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "app.h"
#include "panel_registry.h"
#include "panels/preview_state.h"
#include "rensga/editor/inspector_model.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/loop.h"
#include "rensga/core/scene_manager.h"
#include "render/lit_view.h"
#include "rensga/render/gl.h"

namespace rensga {
namespace editor {

namespace {

struct PlayGl {
  GlRenderer gl;
  bool inited = false;
  u32 w = 0, h = 0;
  uint64_t boundId = 0;
  std::vector<u32> tex;
};
PlayGl* g_pg = nullptr;  // never deleted: its destructor would run after the GL context is gone

PlayGl& playGl() {
  if (!g_pg) g_pg = new PlayGl();
  return *g_pg;
}

// Uploads the World's meshes and textures to the renderer and points the 2D components at them.
void bindWorld(PlayGl& pg, EditorWorld& ew, const PreviewRules& rules, const PreviewOptions& po) {
  const ConsoleTargetData& ct = ew.consoleTarget();
  const u32 w = ct.width ? ct.width : 480, h = ct.height ? ct.height : 272;
  if (!pg.inited) {
    pg.inited = pg.gl.init(w, h);
    pg.w = w; pg.h = h;
    if (!pg.inited) { EditorLog::instance().error("play: the Game view renderer failed to initialise"); return; }
    pg.gl.setOffscreen(true);
  } else if (pg.w != w || pg.h != h) {
    pg.gl.setVirtualResolution(w, h);
    pg.w = w; pg.h = h;
  }
  pg.gl.releaseUserTextures();
  pg.gl.setMeshes(ew.loaded().meshes);
  const PackAssets& as = ew.loaded().assets;
  pg.tex.assign(as.textureCount, 0);
  for (u32 i = 0; i < as.textureCount; ++i) {
    const PackTexRec& t = as.textures[i];
    TextureDesc d;
    d.width = t.width; d.height = t.height; d.format = static_cast<u8>(t.format);
    d.pixels = as.texturePixels(t); d.palette = as.texturePalette(t);
    d.storedWidth = t.storedWidth; d.storedHeight = t.storedHeight; d.flags = t.flags;
    // F5.07: the texture as the target stores it (decoded through the target's cooker plugin), so palette banding,
    // 16-bit colour and the size limit show in the Game view.
    if (po.consoleTextures && ew.consolePreview()) {
      char g[40];
      t.guid.format(g);
      if (const ConsolePreview::Texture* ct = ew.consolePreview()->find(g))
        if (!ct->rgba.rgba.empty() && ct->rgba.width && ct->rgba.height) {
          d.width = ct->rgba.width; d.height = ct->rgba.height; d.format = 0;
          d.pixels = ct->rgba.rgba.data(); d.palette = nullptr;
          d.storedWidth = 0; d.storedHeight = 0; d.flags = 0;
        }
    }
    pg.tex[i] = pg.gl.uploadTexture(d);
  }
  (void)rules;
  GfxAssets::bind(&ew.view(), &ew.loaded().assets, pg.tex.data(), as.textureCount);
  SceneManager::bind(&ew.view(), ew.sceneIndex());
  pg.boundId = ew.id();
}

// ---- F5.07 post pass: quantize to 5-6-5 with an ordered 4x4 dither (the target's 16-bit framebuffer) ----
struct PostPass {
  unsigned prog = 0, vao = 0;
  RenderTarget rt;
};
PostPass* g_post = nullptr;  // never deleted (GL context)

unsigned compileShader(unsigned type, const char* src) {
  unsigned s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  int ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, nullptr, log); std::fprintf(stderr, "preview shader: %s\n", log); }
  return s;
}

// Returns the texture to show: `src` quantized (and dithered) into the pass's own target, or `src` itself when off.
unsigned postProcess(unsigned src, int w, int h, bool quantize, bool dither, bool nearest) {
  if (!quantize && !dither) return src;
  if (!g_post) g_post = new PostPass();
  PostPass& p = *g_post;
  if (!p.prog) {
    static const char* vs = "#version 330 core\nvoid main(){ vec2 c = vec2((gl_VertexID<<1)&2, gl_VertexID&2); gl_Position = vec4(c*2.0-1.0, 0.0, 1.0); }\n";
    static const char* fs = R"(#version 330 core
uniform sampler2D uTex; uniform int uQuant; uniform int uDither;
out vec4 o;
const float B[16] = float[16](0.,8.,2.,10., 12.,4.,14.,6., 3.,11.,1.,9., 15.,7.,13.,5.);
void main(){
  ivec2 p = ivec2(gl_FragCoord.xy);
  vec3 c = texelFetch(uTex, p, 0).rgb;
  vec3 lv = uQuant != 0 ? vec3(31.0, 63.0, 31.0) : vec3(255.0);
  float t = (B[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0 - 0.5;
  vec3 v = c * lv + (uDither != 0 ? t : 0.0);
  o = vec4(clamp(floor(v + 0.5) / lv, 0.0, 1.0), 1.0);
})";
    unsigned pr = glCreateProgram(), v = compileShader(GL_VERTEX_SHADER, vs), f = compileShader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(pr, v); glAttachShader(pr, f); glLinkProgram(pr);
    glDeleteShader(v); glDeleteShader(f);
    int ok = 0;
    glGetProgramiv(pr, GL_LINK_STATUS, &ok);
    if (!ok) { glDeleteProgram(pr); p.prog = 0; return src; }
    p.prog = pr;
    glGenVertexArrays(1, &p.vao);
  }
  p.rt.ensure(w, h, 0, nearest);
  GLint fbo = 0, vp[4], prog = 0, vao = 0, tex = 0, active = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
  glGetIntegerv(GL_VIEWPORT, vp);
  glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex);
  const GLboolean depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE), scissor = glIsEnabled(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_FRAMEBUFFER, p.rt.fbo);
  glViewport(0, 0, w, h);
  glUseProgram(p.prog);
  glBindTexture(GL_TEXTURE_2D, src);
  glUniform1i(glGetUniformLocation(p.prog, "uTex"), 0);
  glUniform1i(glGetUniformLocation(p.prog, "uQuant"), quantize ? 1 : 0);
  glUniform1i(glGetUniformLocation(p.prog, "uDither"), dither ? 1 : 0);
  glBindVertexArray(p.vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(static_cast<GLuint>(vao));
  glUseProgram(static_cast<GLuint>(prog));
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex));
  glActiveTexture(static_cast<GLenum>(active));
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(fbo));
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  if (depth) glEnable(GL_DEPTH_TEST);
  if (blend) glEnable(GL_BLEND);
  if (cull) glEnable(GL_CULL_FACE);
  if (scissor) glEnable(GL_SCISSOR_TEST);
  return p.rt.tex;
}

std::string bytesText(uint64_t b) {
  char t[32];
  if (b >= (1u << 20)) std::snprintf(t, sizeof t, "%.1f MB", b / 1048576.0);
  else std::snprintf(t, sizeof t, "%.1f KB", b / 1024.0);
  return t;
}

}  // namespace

// F6.02 (B3): the Scene view's lit pass shares this 5-6-5 / dither pass.
unsigned previewPostProcess(unsigned src, int w, int h, bool quantize, bool dither, bool nearest) {
  return postProcess(src, w, h, quantize, dither, nearest);
}

void gameViewRelease() {
  if (!g_pg) return;
  if (g_pg->inited) g_pg->gl.releaseUserTextures();
  g_pg->boundId = 0;
  g_pg->tex.clear();
}

static void drawWarnBadge(ImDrawList* dl, ImVec2 p, float size) {  // triangle with "!" (status is never color alone)
  const ImU32 c = ImGui::GetColorU32(brandToken("warn"));
  dl->AddTriangleFilled(ImVec2(p.x + size * 0.5f, p.y), ImVec2(p.x, p.y + size), ImVec2(p.x + size, p.y + size), c);
  dl->AddText(ImVec2(p.x + size * 0.5f - 2.5f, p.y + size * 0.18f), ImGui::GetColorU32(brandToken("warn-bg")), "!");
}

void drawGameView(App& a) {
  if (!a.showGame2) return;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  bool open = ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::PopStyleVar();
  if (!open) { a.gameFocused = false; ImGui::End(); return; }
  a.gameFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && a.session.active();
  const bool playing = a.session.active() && a.session.world() && a.session.world()->valid();

  // strip: options (left), session state (right)
  ImGui::SetCursorPos(ImVec2(8, ImGui::GetCursorPosY() + 4));  // below the tab bar of a docked window
  ImGui::Checkbox("Integer scale", &a.gameIntegerScale);
  ImGui::SameLine();
  ImGui::Checkbox("Stats", &a.gameStats);
  PreviewOptions& po = previewOptions();
  const TargetModule* tm = a.targetModule();
  const ConsoleTargetData& ctd = consoleTargetFor(a.project.root);
  const PreviewRules pr = effectivePreviewRules(tm, ctd);
  if (!po.initialized) {  // first time for this editor session: the target's rules decide
    po.initialized = true;
    po.quantize = pr.colorBits <= 16;
    po.dither = pr.dither;
  }
  ImGui::SameLine();
  if (ImGui::Button("Preview")) ImGui::OpenPopup("##previewopts");
  if (ImGui::BeginPopup("##previewopts")) {
    char t[96];
    std::snprintf(t, sizeof t, "%d-bit colour output (5-6-5)", 16);
    ImGui::BeginDisabled(pr.colorBits > 16);
    ImGui::Checkbox(t, &po.quantize);
    ImGui::EndDisabled();
    ImGui::Checkbox("Ordered dither (4x4)", &po.dither);
    ImGui::Checkbox("Device frame", &po.deviceFrame);
    ImGui::Checkbox("Safe area", &po.safeArea);
    ImGui::Checkbox("Budget bars", &po.bars);
    ImGui::BeginDisabled(pr.maxLightsPerObject <= 0);
    std::snprintf(t, sizeof t, "At most %d lights per object", pr.maxLightsPerObject);
    ImGui::Checkbox(t, &po.lightLimit);
    ImGui::EndDisabled();
    const bool ctOld = po.consoleTextures;
    ImGui::Checkbox("Textures as the target stores them", &po.consoleTextures);
    if (ctOld != po.consoleTextures) playGl().boundId = 0;
    ImGui::EndPopup();
  }
  if (a.session.active()) {
    const char* st = a.session.state() == PlayState::Paused ? "Paused" : "Playing";
    ImGui::SameLine();
    ImGui::TextColored(brandToken("primary"), "%s", st);
  }
  const ImVec2 wp = ImGui::GetWindowPos();
  // the strip's height from the screen cursor: a docked window's tab bar sits above the content, so the window-relative cursor
  // would put the view over the strip
  const float stripH = ImGui::GetCursorScreenPos().y - wp.y + 2;
  const ImVec2 p0(wp.x, wp.y + stripH), sz(ImGui::GetWindowWidth(), ImGui::GetWindowHeight() - stripH);
  if (sz.x < 8 || sz.y < 8) { ImGui::End(); return; }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ImGui::GetColorU32(brandToken("bg")));

  const int rw = pr.width > 0 ? pr.width : 640, rh = pr.height > 0 ? pr.height : 360;
  const float lineH = ImGui::GetTextLineHeight();
  const float labelH = lineH + 16 + (po.bars ? lineH + 12 : 0);
  const float aspect = static_cast<float>(rw) / static_cast<float>(rh);
  const float bezel = po.deviceFrame ? std::max(14.0f, sz.y * 0.05f) : 0.0f;
  const float margin = 24 + bezel * 2;
  float fw = sz.x - margin, fh = fw / aspect;
  if (fh > sz.y - margin - labelH) { fh = sz.y - margin - labelH; fw = fh * aspect; }
  if (a.gameIntegerScale) {
    const float availW = sz.x - margin, availH = sz.y - margin - labelH;
    const float k = std::floor(std::min(availW / rw, availH / rh));
    if (k >= 1.0f) { fw = rw * k; fh = rh * k; }
  }
  fw = std::max(fw, 8.0f); fh = std::max(fh, 8.0f);
  const ImVec2 f0(p0.x + (sz.x - fw) * 0.5f, p0.y + 12 + bezel + (sz.y - margin - labelH - fh) * 0.5f), f1(f0.x + fw, f0.y + fh);
  if (po.deviceFrame) {  // a plain handheld outline: rounded body, screen opening, no brand marks
    const ImVec2 o0(f0.x - bezel, f0.y - bezel), o1(f1.x + bezel, f1.y + bezel);
    dl->AddRectFilled(o0, o1, ImGui::GetColorU32(brandToken("surface")), bezel * 0.8f);
    dl->AddRect(o0, o1, ImGui::GetColorU32(brandToken("border-strong")), bezel * 0.8f, 0, 2.0f);
    dl->AddRectFilled(ImVec2(f0.x - 2, f0.y - 2), ImVec2(f1.x + 2, f1.y + 2), IM_COL32(0, 0, 0, 255));
  }

  if (playing) {
    PlayGl& pg = playGl();
    EditorWorld& ew = *a.session.world();
    if (pg.boundId != ew.id()) bindWorld(pg, ew, pr, po);
    if (pg.inited) {
      if (pr.maxLightsPerObject > 0) ew.setMaxLightsPerObject(po.lightLimit ? static_cast<uint32_t>(pr.maxLightsPerObject) : 8u);
      renderWorld(ew.world(), pg.gl, Color32(40, 60, 90, 255));
      a.session.setFrameStats(pg.gl.lastDrawCalls(), pg.gl.lastSpriteCount());
      const unsigned shown = postProcess(pg.gl.colorTexture(), static_cast<int>(pg.w), static_cast<int>(pg.h), po.quantize, po.dither, a.gameIntegerScale);
      glBindTexture(GL_TEXTURE_2D, shown);
      const GLint filter = a.gameIntegerScale ? GL_NEAREST : GL_LINEAR;
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
      dl->AddImage(static_cast<ImTextureID>(shown), f0, f1, ImVec2(0, 1), ImVec2(1, 0));
    }
  } else if (const unsigned litTex = litGameTexture(a, rw, rh)) {
    // F6.02: the edit-mode scene as the Camera sees it, drawn by the player's renderer (lights, textures, baked colors)
    const unsigned shown = postProcess(litTex, rw, rh, po.quantize, po.dither, a.gameIntegerScale);
    dl->AddImage(static_cast<ImTextureID>(shown), f0, f1, ImVec2(0, 1), ImVec2(1, 0));
  } else {
    SceneDraw sd = gatherScene(a.doc, a.meshes);
    if (sd.camera.found && a.renderer.ready()) {
      a.gameRT.ensure(rw, rh, 0, true);
      RenderOptions ro;
      const CameraInfo& c = sd.camera;
      ro.view = viewLH(c.eye, c.right, c.up, c.fwd);
      ro.proj = perspectiveLH(c.fovDeg, aspect, c.nearClip, c.farClip);
      ro.farClip = c.farClip;
      ImVec4 bg = brandToken("surface-2");
      ro.clear[0] = bg.x; ro.clear[1] = bg.y; ro.clear[2] = bg.z; ro.clear[3] = 1;
      a.renderer.render(a.gameRT, sd, ro);
      const unsigned shown = postProcess(a.gameRT.tex, rw, rh, po.quantize, po.dither, a.gameIntegerScale);
      dl->AddImage(static_cast<ImTextureID>(shown), f0, f1, ImVec2(0, 1), ImVec2(1, 0));
    } else {
      dl->AddRectFilled(f0, f1, ImGui::GetColorU32(brandToken("surface-2")));
      const char* msg = "No Camera in the scene";
      ImVec2 ts = ImGui::CalcTextSize(msg);
      dl->AddText(ImVec2(f0.x + (fw - ts.x) * 0.5f, f0.y + (fh - ts.y) * 0.5f), ImGui::GetColorU32(brandToken("muted")), msg);
    }
  }
  dl->AddRect(f0, f1, ImGui::GetColorU32(a.session.active() ? brandToken("primary") : brandToken("border-strong")), 0, 0,
              a.session.active() ? 2.0f : 1.0f);

  // stats overlay + budget findings
  if (playing && a.gameStats) {
    const PlayStats& s = a.session.stats();
    char l1[200], l2[200], l3[200];
    std::snprintf(l1, sizeof l1, "%.0f fps   sim %.2f ms   frame %llu", s.fps, s.simMs, static_cast<unsigned long long>(s.gameFrames));
    std::snprintf(l2, sizeof l2, "draws %u   sprites %u   triangles %u / %llu", s.draws, s.sprites, s.triangles,
                  static_cast<unsigned long long>(s.trianglesBudget));
    std::snprintf(l3, sizeof l3, "objects %u (%u runtime)   VRAM %s / %s   memory %s / %s", s.objects, s.runtimeObjects,
                  bytesText(s.vramUsed).c_str(), bytesText(s.vramCapacity).c_str(), bytesText(s.memUsed).c_str(),
                  bytesText(s.memCapacity).c_str());
    const char* lines[3] = {l1, l2, l3};
    float tw = 0;
    for (const char* l : lines) tw = std::max(tw, ImGui::CalcTextSize(l).x);
    const float lh = ImGui::GetTextLineHeight();
    dl->AddRectFilled(ImVec2(f0.x + 6, f0.y + 6), ImVec2(f0.x + 6 + tw + 12, f0.y + 6 + lh * 3 + 8), IM_COL32(0, 0, 0, 170), 4);
    for (int i = 0; i < 3; ++i)
      dl->AddText(ImVec2(f0.x + 12, f0.y + 10 + lh * i), IM_COL32(255, 255, 255, 235), lines[i]);
  }
  if (playing) {
    const auto& fs = a.session.findings();
    if (!fs.empty()) {
      const float lh = ImGui::GetTextLineHeight() + 6;
      float y = f1.y - 6 - lh * fs.size();
      for (const BudgetFinding& f : fs) {
        const std::string t = "Over budget: " + f.text;
        const float tw = ImGui::CalcTextSize(t.c_str()).x;
        dl->AddRectFilled(ImVec2(f0.x + 6, y), ImVec2(f0.x + 6 + tw + lh + 12, y + lh - 2), ImGui::GetColorU32(brandToken("warn-bg")), 4);
        drawWarnBadge(dl, ImVec2(f0.x + 10, y + 2), lh - 6);
        dl->AddText(ImVec2(f0.x + 10 + lh, y + 2), ImGui::GetColorU32(brandToken("warn")), t.c_str());
        y += lh;
      }
    }
  }

  if (po.safeArea) {  // inset rectangle from the target's safe area, else 5% on each side (title safe)
    const float sx = static_cast<float>(fw) / rw, sy = static_cast<float>(fh) / rh;
    float l = pr.safeArea.left * sx, t = pr.safeArea.top * sy, r = pr.safeArea.right * sx, b = pr.safeArea.bottom * sy;
    if (l + t + r + b <= 0) { l = r = fw * 0.05f; t = b = fh * 0.05f; }
    const ImU32 c = ImGui::GetColorU32(brandToken("info"));
    dl->AddRect(ImVec2(f0.x + l, f0.y + t), ImVec2(f1.x - r, f1.y - b), c, 0, 0, 1.5f);
    dl->AddText(ImVec2(f0.x + l + 4, f0.y + t + 2), c, "safe area");
  }
  char info[260];
  {
    int n = std::snprintf(info, sizeof info, "%d x %d  %s", rw, rh,
                          tm ? std::string(tm->displayName()).c_str() : (a.project.target.empty() ? "(no target)" : a.project.target.c_str()));
    if (po.quantize) n += std::snprintf(info + n, sizeof info - n, "  16-bit 5-6-5%s", po.dither ? " dithered" : "");
    if (pr.maxLightsPerObject > 0 && po.lightLimit) n += std::snprintf(info + n, sizeof info - n, "  max %d lights/object", pr.maxLightsPerObject);
    if (po.consoleTextures && playing) std::snprintf(info + n, sizeof info - n, "  target textures");
  }
  dl->AddText(ImVec2(f0.x, f1.y + 6), ImGui::GetColorU32(brandToken("text-2")), info);
  if (po.bars) {  // RAM / VRAM / polygons against the ConsoleTarget budgets
    struct Bar { const char* name; double used, cap; bool bytes; };
    Bar bars[3] = {{"RAM", 0, static_cast<double>(ctd.ramBytes), true}, {"VRAM", 0, static_cast<double>(ctd.vramBytes), true},
                   {"Polygons", 0, static_cast<double>(ctd.trianglesPerFrame), false}};
    bool have = false;
    if (playing) {
      const PlayStats& s = a.session.stats();
      bars[0].used = static_cast<double>(s.memUsed); bars[0].cap = static_cast<double>(s.memCapacity ? s.memCapacity : ctd.ramBytes);
      bars[1].used = static_cast<double>(s.vramUsed); bars[1].cap = static_cast<double>(s.vramCapacity ? s.vramCapacity : ctd.vramBytes);
      const ConsolePreview* cpw = a.session.world() ? a.session.world()->consolePreview() : nullptr;
      bars[2].used = static_cast<double>(s.triangles ? s.triangles : (cpw ? cpw->triangles : 0)); bars[2].cap = static_cast<double>(s.trianglesBudget ? s.trianglesBudget : ctd.trianglesPerFrame);
      have = true;
    } else if (const ConsolePreview* cp = scenePreview(a)) {
      std::vector<VramItem> items = framebufferVramItems(ctd);
      for (const ConsolePreview::Texture& t : cp->textures) {
        VramItem vi;
        vi.name = t.guid; vi.kind = "texture"; vi.guid = t.guid; vi.bytes = t.bytes;
        vi.width = t.storedWidth; vi.height = t.storedHeight;
        items.push_back(vi);
      }
      const VramLayout lay = tm ? tm->vramLayout(ctd, items) : linearVramLayout(ctd, items);
      bars[0].used = static_cast<double>(cp->packBytes);
      bars[1].used = static_cast<double>(lay.used);
      bars[2].used = static_cast<double>(cp->triangles);
      have = true;
    }
    const float by = f1.y + 6 + lineH + 4, gap = 10, bw = std::max(60.0f, (std::max(fw, 300.0f) - gap * 2) / 3);
    for (int i = 0; i < 3; ++i) {
      const float bx = f0.x + i * (bw + gap);
      const double frac = bars[i].cap > 0 ? bars[i].used / bars[i].cap : 0;
      const bool over = have && frac > 1.0, near = have && frac > 0.85;
      dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + lineH + 4), ImGui::GetColorU32(brandToken("surface-2")), 3);
      if (have && bars[i].cap > 0)
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * static_cast<float>(std::min(frac, 1.0)), by + lineH + 4),
                          ImGui::GetColorU32(brandToken(over ? "danger-bg" : near ? "warn-bg" : "info-bg")), 3);
      dl->AddRect(ImVec2(bx, by), ImVec2(bx + bw, by + lineH + 4), ImGui::GetColorU32(brandToken(over ? "danger" : near ? "warn" : "border-strong")), 3);
      char t[120];
      if (!have) std::snprintf(t, sizeof t, "%s  -", bars[i].name);
      else if (bars[i].bytes) std::snprintf(t, sizeof t, "%s%s %s / %s", over ? "[!] " : "", bars[i].name, bytesText(static_cast<uint64_t>(bars[i].used)).c_str(), bytesText(static_cast<uint64_t>(bars[i].cap)).c_str());
      else std::snprintf(t, sizeof t, "%s%s %.0f / %.0f", over ? "[!] " : "", bars[i].name, bars[i].used, bars[i].cap);
      dl->PushClipRect(ImVec2(bx, by), ImVec2(bx + bw, by + lineH + 4), true);
      dl->AddText(ImVec2(bx + 5, by + 2), ImGui::GetColorU32(brandToken(over ? "danger" : "text")), t);
      dl->PopClipRect();
    }
  }
  const char* note = playing ? (a.gameFocused ? "Game view has the keyboard and gamepad" : "Click the Game view to give it the keyboard")
                             : "Camera preview \xE2\x80\x94 press Play to run the scene";
  ImVec2 ns = ImGui::CalcTextSize(note);
  if (f0.x + ImGui::CalcTextSize(info).x + 24 < f1.x - ns.x)  // the hint gives way to the preview line on a narrow view
    dl->AddText(ImVec2(f1.x - ns.x, f1.y + 6), ImGui::GetColorU32(brandToken("muted")), note);
  ImGui::End();
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(game_view, "Game", drawGameView, nullptr, nullptr, 70, 0, ::rensga::editor::PanelDraw::Body)

}  // namespace editor
}  // namespace rensga
