// Game window. Edit mode (MVP-50): the scene seen through its Camera component at the active target's preview resolution.
// Play mode (F3.04): the running World drawn with the desktop player's renderer (GlRenderer + renderWorld: 3D meshes and the
// 2D batch) into an offscreen target at the ConsoleTarget resolution, letterboxed (optional integer scale), with a stats overlay and
// live budget warnings. Resolution comes from TargetModule::previewRules() or, without a TargetModule, from the ConsoleTarget
// data itself; nothing here names a console.
#include <SDL_opengl.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "app.h"
#include "rensga/core/gameplay.h"
#include "rensga/core/loop.h"
#include "rensga/core/scene_manager.h"
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
void bindWorld(PlayGl& pg, EditorWorld& ew) {
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
    pg.tex[i] = pg.gl.uploadTexture(d);
  }
  GfxAssets::bind(&ew.view(), &ew.loaded().assets, pg.tex.data(), as.textureCount);
  SceneManager::bind(&ew.view(), ew.sceneIndex());
  pg.boundId = ew.id();
}

std::string bytesText(uint64_t b) {
  char t[32];
  if (b >= (1u << 20)) std::snprintf(t, sizeof t, "%.1f MB", b / 1048576.0);
  else std::snprintf(t, sizeof t, "%.1f KB", b / 1024.0);
  return t;
}

}  // namespace

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
  ImGui::SetCursorPos(ImVec2(8, 4));
  ImGui::Checkbox("Integer scale", &a.gameIntegerScale);
  ImGui::SameLine();
  ImGui::Checkbox("Stats", &a.gameStats);
  if (a.session.active()) {
    const char* st = a.session.state() == PlayState::Paused ? "Paused" : "Playing";
    ImGui::SameLine();
    ImGui::TextColored(brandToken("primary"), "%s", st);
  }
  const float stripH = ImGui::GetCursorPosY() + 2;

  const ImVec2 wp = ImGui::GetWindowPos();
  const ImVec2 p0(wp.x, wp.y + stripH), sz(ImGui::GetWindowWidth(), ImGui::GetWindowHeight() - stripH);
  if (sz.x < 8 || sz.y < 8) { ImGui::End(); return; }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ImGui::GetColorU32(brandToken("bg")));

  const TargetModule* tm = a.targetModule();
  const ConsoleTargetData& ctd = consoleTargetFor(a.project.root);
  PreviewRules pr = tm ? tm->previewRules(ctd) : PreviewRules{static_cast<int>(ctd.width), static_cast<int>(ctd.height)};
  const int rw = pr.width > 0 ? pr.width : 640, rh = pr.height > 0 ? pr.height : 360;
  const float labelH = ImGui::GetTextLineHeight() + 16;
  const float aspect = static_cast<float>(rw) / static_cast<float>(rh);
  float fw = sz.x - 24, fh = fw / aspect;
  if (fh > sz.y - 24 - labelH) { fh = sz.y - 24 - labelH; fw = fh * aspect; }
  if (a.gameIntegerScale) {
    const float availW = sz.x - 24, availH = sz.y - 24 - labelH;
    const float k = std::floor(std::min(availW / rw, availH / rh));
    if (k >= 1.0f) { fw = rw * k; fh = rh * k; }
  }
  fw = std::max(fw, 8.0f); fh = std::max(fh, 8.0f);
  const ImVec2 f0(p0.x + (sz.x - fw) * 0.5f, p0.y + 12 + (sz.y - 24 - labelH - fh) * 0.5f), f1(f0.x + fw, f0.y + fh);

  if (playing) {
    PlayGl& pg = playGl();
    EditorWorld& ew = *a.session.world();
    if (pg.boundId != ew.id()) bindWorld(pg, ew);
    if (pg.inited) {
      renderWorld(ew.world(), pg.gl, Color32(40, 60, 90, 255));
      a.session.setFrameStats(pg.gl.lastDrawCalls(), pg.gl.lastSpriteCount());
      glBindTexture(GL_TEXTURE_2D, pg.gl.colorTexture());
      const GLint filter = a.gameIntegerScale ? GL_NEAREST : GL_LINEAR;
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
      dl->AddImage(static_cast<ImTextureID>(pg.gl.colorTexture()), f0, f1, ImVec2(0, 1), ImVec2(1, 0));
    }
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
      dl->AddImage(static_cast<ImTextureID>(a.gameRT.tex), f0, f1, ImVec2(0, 1), ImVec2(1, 0));
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

  char info[200];
  std::snprintf(info, sizeof info, "%d x %d  %s", rw, rh,
                tm ? std::string(tm->displayName()).c_str() : (a.project.target.empty() ? "(no target)" : a.project.target.c_str()));
  dl->AddText(ImVec2(f0.x, f1.y + 6), ImGui::GetColorU32(brandToken("text-2")), info);
  const char* note = playing ? (a.gameFocused ? "Game view has the keyboard and gamepad" : "Click the Game view to give it the keyboard")
                             : "Camera preview \xE2\x80\x94 press Play to run the scene";
  ImVec2 ns = ImGui::CalcTextSize(note);
  dl->AddText(ImVec2(f1.x - ns.x, f1.y + 6), ImGui::GetColorU32(brandToken("muted")), note);
  ImGui::End();
}

}  // namespace editor
}  // namespace rensga
