// F1.03: splash screen, project picker, Create Project. Colors come from the embedded palette tokens only.
#define GL_GLEXT_PROTOTYPES 1
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "app.h"
#include "modules.h"
#include "rensga/editor/editor_log.h"

namespace rensga {
namespace editor {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------------------------
void hubInit(App& a, const std::string& homeOverride) {
  a.hub.settings.load(HubSettings::resolveHome(homeOverride));
  fs::path root = RENSGA_SOURCE_DIR;
  if (!a.hub.data.load(root / "editor" / "data" / "hub.json", root / "templates"))
    EditorLog::instance().error("hub: editor/data/hub.json is missing or empty");
  a.hub.api = -1;
  for (size_t i = 0; i < a.hub.data.apis.size(); ++i)
    if (a.hub.data.apiSelectable(i)) { a.hub.api = static_cast<int>(i); break; }
  a.hub.rescan = true;
}

void hubNoteOpened(App& a) {
  if (!a.loaded) return;
  a.hub.settings.lastOpened[a.project.root.string()] = static_cast<long long>(std::time(nullptr));
  a.hub.settings.save();
  a.hub.rescan = true;
}

// ---------------------------------------------------------------------------------------------
// Splash background: a slowly drifting triangular lattice (GLSL 3.3)
// ---------------------------------------------------------------------------------------------
static const char* kSplashVs = R"(#version 330 core
void main(){
  vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
})";

static const char* kSplashFs = R"(#version 330 core
uniform vec2 uRes; uniform float uTime;
uniform vec3 uBg, uLine, uNode, uPulse, uGold;
out vec4 o;
const float H = 0.8660254;
float hash(vec2 p){ p = fract(p * vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x * p.y); }
void main(){
  vec2 px = gl_FragCoord.xy;
  vec2 uv = px / uRes;
  float asp = uRes.x / uRes.y;
  float cell = 58.0;
  vec2 p = (px - 0.5 * uRes + vec2(uTime * 7.0, uTime * 4.0)) / cell;
  vec2 n[3] = vec2[3](vec2(0.0, 1.0), vec2(-H, -0.5), vec2(H, -0.5));
  float f[3], d[3], lines = 0.0, glow = 0.0;
  vec3 pulseCol = vec3(0.0);
  float pulse = 0.0, goldPulse = 0.0;
  for (int k = 0; k < 3; ++k) {
    f[k] = dot(p, n[k]) / H;
    float id = floor(f[k] + 0.5);
    d[k] = abs(f[k] - id) * H * cell;
    lines = max(lines, 1.0 - smoothstep(0.35, 1.25, d[k]));
    float lh = hash(vec2(id, float(k) + 1.0));
    if (lh > 0.80) {
      vec2 tg = vec2(n[k].y, -n[k].x);
      float s = dot(p, tg);
      float sp = 0.10 + 0.10 * hash(vec2(id, 9.0 + float(k)));
      float q = fract(s / 7.0 - uTime * sp + lh * 11.0);
      float head = exp(-q * 7.0) * (1.0 - exp(-q * 60.0));
      float g = 1.0 - smoothstep(0.4, 6.0, d[k]);
      float v = head * g;
      if (lh > 0.985) goldPulse = max(goldPulse, v); else pulse = max(pulse, v);
    }
  }
  vec2 nid = vec2(floor(f[0] + 0.5), floor(f[1] + 0.5));
  float dn = length(vec2(d[0], d[1]));
  float nh = hash(nid + 7.0);
  float node = (1.0 - smoothstep(1.2, 3.6, dn)) * (0.35 + 0.65 * step(0.45, nh));
  float tw = 0.65 + 0.35 * sin(uTime * 0.9 + nh * 40.0);
  float goldNode = (nh > 0.985) ? (1.0 - smoothstep(1.5, 9.0, dn)) * (0.45 + 0.55 * sin(uTime * 1.2 + nh * 60.0) * 0.5 + 0.275) : 0.0;
  // keep the middle calm so the logo reads, and fade the lattice toward the edges
  vec2 c = (uv - vec2(0.5, 0.56)) * vec2(asp, 1.0);
  float calm = 0.30 + 0.70 * smoothstep(0.10, 0.62, length(c));
  float edge = 1.0 - 0.55 * smoothstep(0.55, 1.05, length((uv - 0.5) * vec2(asp, 1.0)));
  vec3 col = uBg;
  col = mix(col, uLine, lines * 0.30 * calm * edge);
  col = mix(col, uPulse, clamp(pulse, 0.0, 1.0) * 0.55 * calm * edge);
  col = mix(col, uNode, node * tw * 0.55 * calm * edge);
  col = mix(col, uGold, clamp(goldPulse, 0.0, 1.0) * 0.40 * edge);
  col = mix(col, uGold, clamp(goldNode, 0.0, 1.0) * 0.70 * edge);
  col *= 1.0 - 0.45 * smoothstep(0.50, 1.0, length((uv - 0.5) * vec2(asp * 0.8, 1.0)));  // vignette
  o = vec4(col, 1.0);
})";

namespace {
struct SplashGl { unsigned prog = 0, vao = 0; bool tried = false; } g_splash;

unsigned compileSh(unsigned type, const char* src) {
  unsigned s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  int ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[1024]; glGetShaderInfoLog(s, sizeof log, nullptr, log); std::fprintf(stderr, "splash shader: %s\n", log); }
  return s;
}
void uniform3(unsigned prog, const char* n, ImVec4 c) { glUniform3f(glGetUniformLocation(prog, n), c.x, c.y, c.z); }
}  // namespace

void drawSplashBackground(float t, int w, int h) {
  if (!g_splash.tried) {
    g_splash.tried = true;
    unsigned v = compileSh(GL_VERTEX_SHADER, kSplashVs), f = compileSh(GL_FRAGMENT_SHADER, kSplashFs);
    unsigned p = glCreateProgram();
    glAttachShader(p, v); glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v); glDeleteShader(f);
    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (ok) { g_splash.prog = p; glGenVertexArrays(1, &g_splash.vao); }
    else glDeleteProgram(p);
  }
  if (!g_splash.prog) return;  // the flat clear color stays
  glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_SCISSOR_TEST);
  glUseProgram(g_splash.prog);
  glUniform2f(glGetUniformLocation(g_splash.prog, "uRes"), static_cast<float>(w), static_cast<float>(h));
  glUniform1f(glGetUniformLocation(g_splash.prog, "uTime"), t);
  uniform3(g_splash.prog, "uBg", brandToken("bg"));
  uniform3(g_splash.prog, "uLine", brandRamp("cerrado.700"));
  uniform3(g_splash.prog, "uNode", brandRamp("cerrado.500"));
  uniform3(g_splash.prog, "uPulse", brandRamp("cerrado.400"));
  uniform3(g_splash.prog, "uGold", brandToken("primary"));
  glBindVertexArray(g_splash.vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glUseProgram(0);
}

static void centerText(ImDrawList* dl, ImFont* font, float size, float cx, float y, ImU32 col, const char* text) {
  ImGui::PushFont(font, size);
  ImVec2 ts = ImGui::CalcTextSize(text);
  dl->AddText(font, size, ImVec2(cx - ts.x * 0.5f, y), col, text);
  ImGui::PopFont();
}

void drawSplashUi(App& a, float) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
  ImGui::Begin("##splash", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float W = vp->Size.x, H = vp->Size.y;
  const ImVec2 o = vp->Pos;
  // Lockup: icon height 2.2 x wordmark size, gap 0.3 x icon, clear space >= 1/4 icon on every side.
  const float fs = 54.0f, icon = 2.2f * fs, gap = 0.3f * icon;
  ImGui::PushFont(a.fonts.uiBold, fs);
  const char* word = "Rensga Engine";
  ImVec2 ts = ImGui::CalcTextSize(word);
  ImGui::PopFont();
  const float total = icon + gap + ts.x, x0 = o.x + (W - total) * 0.5f, cy = o.y + H * 0.43f;
  drawPequi(dl, ImVec2(x0, cy - icon * 0.5f), icon);
  dl->AddText(a.fonts.uiBold, fs, ImVec2(x0 + icon + gap, cy - ts.y * 0.5f), ImGui::GetColorU32(brandToken("text")), word);
  const ImU32 muted = ImGui::GetColorU32(brandToken("muted"));
  char ver[96];
  std::snprintf(ver, sizeof ver, "Version %s  \xc2\xb7  Editor 0  \xc2\xb7  Open-source engine for retro consoles", RENSGA_VERSION);
  centerText(dl, a.fonts.ui, 14.0f, o.x + W * 0.5f, cy + icon * 0.5f + icon * 0.25f + 6.0f, muted, ver);
  // status + progress
  dl->AddText(a.fonts.ui, 13.0f, ImVec2(o.x + 24, o.y + H - 40), muted, a.hub.status.c_str());
  const float bx0 = o.x + 24, bx1 = o.x + W - 24, by = o.y + H - 16;
  dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + 2), ImGui::GetColorU32(brandToken("border")), 1.0f);
  dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx0 + (bx1 - bx0) * std::clamp(a.hub.progress, 0.0f, 1.0f), by + 2),
                    ImGui::GetColorU32(brandRamp("cerrado.500")), 1.0f);
  dl->AddRect(o, ImVec2(o.x + W, o.y + H), ImGui::GetColorU32(brandToken("border-strong")), 0, 0, 1.0f);
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);
}

// ---------------------------------------------------------------------------------------------
// Picker
// ---------------------------------------------------------------------------------------------
static std::string upper(std::string s) { for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }

// Target label and color for a profile id: palette targets, else the id itself.
static void targetLook(const std::string& id, ImVec4& col, std::string& label) {
  if (!brandTarget(id, col, label)) { col = brandToken("surface-2"); label = upper(id); }
}

static std::string buildLabel(const App& a, const std::string& id) {
  for (const HubBuildTarget& b : a.hub.data.buildTargets) if (b.id == id) return b.label;
  return id;
}

static std::string fmtTime(long long t) {
  if (t <= 0) return "never";
  std::time_t tt = static_cast<std::time_t>(t);
  std::tm tmv{};
  localtime_r(&tt, &tmv);
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &tmv);
  return buf;
}

static void badge(ImDrawList* dl, ImVec2 pos, const std::string& label, ImVec4 col, float lineH) {
  ImVec2 ts = ImGui::CalcTextSize(label.c_str());
  ImVec2 a(pos.x, pos.y + 2), b(pos.x + ts.x + 16, pos.y + lineH - 2);
  dl->AddRectFilled(a, b, ImGui::GetColorU32(col), 5.0f);
  dl->AddText(ImVec2(a.x + 8, a.y + (b.y - a.y - ts.y) * 0.5f), ImGui::GetColorU32(brandRamp("soil.50")), label.c_str());
}

static bool primaryButton(const char* label, ImVec2 size = ImVec2(0, 0)) {
  ImGui::PushStyleColor(ImGuiCol_Button, brandToken("primary"));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brandToken("primary-hover"));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, brandToken("primary-hover"));
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("on-primary"));
  bool r = ImGui::Button(label, size);
  ImGui::PopStyleColor(4);
  return r;
}

static void openEntry(App& a, int i) {
  if (i < 0 || i >= static_cast<int>(a.hub.entries.size())) return;
  a.pendingProject = a.hub.entries[i].dir.string();
}

void drawPicker(App& a) {
  HubState& h = a.hub;
  if (h.rescan) { h.entries = listProjects(h.settings); h.rescan = false; if (h.sel >= static_cast<int>(h.entries.size())) h.sel = -1; }
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(vp->Size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 24));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
  ImGui::Begin("##picker", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  // Header: lockup (icon 2.2 x font, gap 0.3 x icon) and the Create Project button
  const float fs = 22.0f, icon = 2.2f * fs, gap = 0.3f * icon;
  ImVec2 p0 = ImGui::GetCursorScreenPos();
  drawPequi(dl, p0, icon);
  ImGui::PushFont(a.fonts.uiBold, fs);
  ImVec2 ts = ImGui::CalcTextSize("Rensga Engine");
  dl->AddText(ImVec2(p0.x + icon + gap, p0.y + (icon - ts.y) * 0.5f), ImGui::GetColorU32(brandToken("text")), "Rensga Engine");
  ImGui::PopFont();
  ImGui::Dummy(ImVec2(icon + gap + ts.x, icon));
  const float avail = ImGui::GetContentRegionAvail().x;
  ImGui::SameLine();
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, avail - (icon + gap + ts.x) - 160));
  ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (icon - 34) * 0.5f);
  // RULES §7: one gold action at a time. Gold moves to Open once a project is selected.
  const bool hasSel = h.sel >= 0;
  if (hasSel ? ImGui::Button("Create Project", ImVec2(160, 34)) : primaryButton("Create Project", ImVec2(160, 34))) h.openCreate = true;
  ImGui::Spacing();
  // Projects | Modules tabs (Unity Hub style). The Modules tab replaces the project list and the Open button.
  if (drawHubTabs(a, 52)) {
    ImGui::Spacing();
    ImGui::SameLine(ImGui::GetWindowWidth() - 28 - 90);
    if (ImGui::Button("Quit", ImVec2(90, 32))) a.quitRequested = true;
    ImGui::End();
    ImGui::PopStyleVar(3);
    return;
  }
  ImGui::PushFont(a.fonts.uiBold, 17);
  ImGui::PushStyleColor(ImGuiCol_Text, brandRamp("cerrado.300"));
  ImGui::TextUnformatted("Projects");
  ImGui::PopStyleColor();
  ImGui::PopFont();
  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
  ImGui::Text("  in %s", h.settings.projectsPath.string().c_str());
  ImGui::PopStyleColor();
  ImGui::SameLine();
  if (ImGui::SmallButton("Change...")) { h.openChange = true; }
  ImGui::Spacing();

  const float footer = 52;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, brandToken("surface"));
  ImGui::BeginChild("##projects", ImVec2(0, -footer), ImGuiChildFlags_Borders);
  if (h.entries.empty()) {
    ImVec2 c = ImGui::GetContentRegionAvail();
    const char* t1 = "No projects yet";
    ImGui::PushFont(a.fonts.uiBold, 18);
    ImVec2 s1 = ImGui::CalcTextSize(t1);
    ImGui::SetCursorPos(ImVec2((c.x - s1.x) * 0.5f, c.y * 0.38f));
    ImGui::TextUnformatted(t1);
    ImGui::PopFont();
    std::string t2 = "No project was found in " + h.settings.projectsPath.string() + ". Create one to get started.";
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
    ImVec2 s2 = ImGui::CalcTextSize(t2.c_str());
    ImGui::SetCursorPos(ImVec2(std::max(8.0f, (c.x - s2.x) * 0.5f), ImGui::GetCursorPosY() + 6));
    ImGui::TextUnformatted(t2.c_str());
    ImGui::PopStyleColor();
  } else if (ImGui::BeginTable("##tbl", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_PadOuterX)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.8f);
    ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 84);
    ImGui::TableSetupColumn("Build", ImGuiTableColumnFlags_WidthFixed, 90);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn("Last opened", ImGuiTableColumnFlags_WidthFixed, 140);
    ImGui::TableHeadersRow();
    const float rowH = 34;
    for (int i = 0; i < static_cast<int>(h.entries.size()); ++i) {
      const ProjectEntry& e = h.entries[i];
      ImGui::TableNextRow(ImGuiTableRowFlags_None, rowH);
      ImGui::TableSetColumnIndex(0);
      ImGui::PushID(i);
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
      ImGui::PushFont(a.fonts.uiBold, 15);
      if (ImGui::Selectable(e.name.c_str(), h.sel == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, rowH - 10))) {
        h.sel = i;
        if (ImGui::IsMouseDoubleClicked(0)) openEntry(a, i);
      }
      ImGui::PopFont();
      ImGui::PopID();
      ImGui::TableSetColumnIndex(1);
      ImVec4 tc; std::string tl;
      targetLook(e.profile, tc, tl);
      badge(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), tl, tc, rowH - 4);
      ImGui::TableSetColumnIndex(2);
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(buildLabel(a, e.buildTarget).c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("muted"));
      ImGui::PushFont(a.fonts.mono, 13);
      ImGui::TextUnformatted(e.dir.string().c_str());
      ImGui::PopFont();
      ImGui::PopStyleColor();
      ImGui::TableSetColumnIndex(4);
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("text-2"));
      ImGui::TextUnformatted(fmtTime(e.lastOpened).c_str());
      ImGui::PopStyleColor();
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::Spacing();
  if (!h.error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
    ImGui::TextUnformatted(h.error.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
  }
  ImGui::SameLine(ImGui::GetWindowWidth() - 28 - 120 - 8 - 90);
  if (ImGui::Button("Quit", ImVec2(90, 32))) a.quitRequested = true;
  ImGui::SameLine();
  ImGui::BeginDisabled(h.sel < 0);
  if (hasSel ? primaryButton("Open", ImVec2(120, 32)) : ImGui::Button("Open", ImVec2(120, 32))) openEntry(a, h.sel);
  ImGui::EndDisabled();
  ImGui::End();
  ImGui::PopStyleVar(3);
}

// ---------------------------------------------------------------------------------------------
// Create Project dialog and projects path popup
// ---------------------------------------------------------------------------------------------
static void suggestName(App& a) {
  std::string base = "MyGame";
  std::string n = base;
  for (int i = 2; !validateProjectName(n, a.hub.settings.projectsPath).empty() && i < 100; ++i) n = base + std::to_string(i);
  std::snprintf(a.hub.name, sizeof a.hub.name, "%s", n.c_str());
}

static bool submitCreate(App& a) {
  HubState& h = a.hub;
  if (h.api < 0 || h.build < 0) return false;
  fs::path dir;
  std::string err;
  if (!createProject(h.data, h.settings, h.name, h.data.apis[h.api].id, h.data.buildTargets[h.build].id, dir, err)) {
    h.createError = err;
    return false;
  }
  EditorLog::instance().info("project created: " + dir.string());
  h.createError.clear();
  h.rescan = true;
  a.pendingProject = dir.string();
  return true;
}

static void disabledTip(bool disabled) {
  if (disabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) ImGui::SetTooltip("Coming soon");
}

void drawCreateDialogs(App& a) {
  HubState& h = a.hub;
  if (h.openCreate) {
    suggestName(a);
    h.createError.clear();
    h.build = 0;
    ImGui::OpenPopup("Create Project");
    h.openCreate = false;
  }
  if (h.openChange) {
    std::snprintf(h.pathBuf, sizeof h.pathBuf, "%s", h.settings.projectsPath.string().c_str());
    h.pathError.clear();
    ImGui::OpenPopup("Projects Path");
    h.openChange = false;
  }
  ImVec2 c = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(520, 0));
  if (ImGui::BeginPopupModal("Create Project", nullptr, ImGuiWindowFlags_NoResize)) {
    ImGui::PushFont(a.fonts.uiBold, 15);
    ImGui::TextUnformatted("Project name");
    ImGui::PopFont();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool enter = ImGui::InputText("##name", h.name, sizeof h.name, ImGuiInputTextFlags_EnterReturnsTrue);
    std::string nameErr = validateProjectName(h.name, h.settings.projectsPath);
    ImGui::PushStyleColor(ImGuiCol_Text, nameErr.empty() || !h.name[0] ? brandToken("muted") : brandToken("danger"));
    if (!nameErr.empty() && h.name[0]) ImGui::TextUnformatted(nameErr.c_str());
    else ImGui::TextWrapped("Created in %s", (h.settings.projectsPath / h.name).string().c_str());
    ImGui::PopStyleColor();
    ImGui::Spacing();
    ImGui::PushFont(a.fonts.uiBold, 15);
    ImGui::TextUnformatted("Target API");
    ImGui::PopFont();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
    ImGui::PushStyleColor(ImGuiCol_Border, brandToken("border-strong"));
    for (size_t i = 0; i < h.data.apis.size(); ++i) {
      ImVec4 col; std::string label;
      targetLook(h.data.apis[i].id, col, label);
      bool ok = h.data.apiSelectable(i);
      ImGui::BeginDisabled(!ok);
      if (i) ImGui::SameLine(0, 20);
      ImGui::RadioButton(label.c_str(), &h.api, static_cast<int>(i));
      ImGui::EndDisabled();
      disabledTip(!ok);
    }
    ImGui::Spacing();
    ImGui::PushFont(a.fonts.uiBold, 15);
    ImGui::TextUnformatted("Target build");
    ImGui::PopFont();
    for (size_t i = 0; i < h.data.buildTargets.size(); ++i) {
      const HubBuildTarget& b = h.data.buildTargets[i];
      ImGui::BeginDisabled(!b.available);
      if (i) ImGui::SameLine(0, 20);
      ImGui::RadioButton(b.label.c_str(), &h.build, static_cast<int>(i));
      ImGui::EndDisabled();
      disabledTip(!b.available);
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    if (h.api >= 0) {   // the target's build support module is missing: say so, project creation still works
      std::string miss = missingModuleLine(a, h.data.apis[h.api].id);
      if (!miss.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, brandToken("warn"));
        ImGui::TextUnformatted("Warning:");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 400);
        ImGui::TextUnformatted(miss.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::SmallButton("Open Hub > Modules")) { openModulesFromHub(a); ImGui::CloseCurrentPopup(); }
      }
    }
    if (!h.createError.empty()) {
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
      ImGui::TextWrapped("%s", h.createError.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    bool can = nameErr.empty() && h.api >= 0;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 10 - 110 - 8 - 110);
    if (ImGui::Button("Cancel", ImVec2(110, 32))) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    ImGui::BeginDisabled(!can);
    if (primaryButton("Create", ImVec2(110, 32)) || (enter && can)) {
      if (submitCreate(a)) ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
  }
  ImGui::SetNextWindowPos(c, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(560, 0));
  if (ImGui::BeginPopupModal("Projects Path", nullptr, ImGuiWindowFlags_NoResize)) {
    ImGui::TextUnformatted("Folder that holds your projects (created if missing)");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    bool enter = ImGui::InputText("##path", h.pathBuf, sizeof h.pathBuf, ImGuiInputTextFlags_EnterReturnsTrue);
    if (!h.pathError.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger"));
      ImGui::TextUnformatted(h.pathError.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    if (ImGui::Button("Cancel", ImVec2(96, 0))) ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (primaryButton("Apply", ImVec2(96, 0)) || enter) {
      std::error_code ec;
      fs::path p = h.pathBuf;
      fs::create_directories(p, ec);
      if (!h.pathBuf[0] || ec || !fs::is_directory(p, ec)) h.pathError = "Cannot use that folder.";
      else { h.settings.projectsPath = p; h.settings.save(); h.rescan = true; h.sel = -1; ImGui::CloseCurrentPopup(); }
    }
    ImGui::EndPopup();
  }
}

// --ui-event hooks: hub-new, hub-name:<n>, hub-submit, hub-open:<index>, hub-picker, hub-change
bool hubHook(App& a, const std::string& act, const std::string& arg) {
  HubState& h = a.hub;
  if (act == "hub-new") h.openCreate = true;
  else if (act == "hub-name") { ImGui::ClearActiveID(); std::snprintf(h.name, sizeof h.name, "%s", arg.c_str()); }
  else if (act == "hub-submit") submitCreate(a);
  else if (act == "hub-open") openEntry(a, std::atoi(arg.c_str()));
  else if (act == "hub-picker") h.goPicker = true;
  else if (act == "hub-change") h.openChange = true;
  else return false;
  return true;
}

// ---------------------------------------------------------------------------------------------
// Window > Layouts > Save Layout As
// ---------------------------------------------------------------------------------------------
void drawSaveLayoutDialog(App& a) {
  if (a.openSaveLayout) { a.layoutName[0] = 0; ImGui::OpenPopup("Save Layout As"); a.openSaveLayout = false; }
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (!ImGui::BeginPopupModal("Save Layout As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::TextUnformatted("Layout name");
  ImGui::SetNextItemWidth(320);
  if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
  bool enter = ImGui::InputText("##ln", a.layoutName, sizeof a.layoutName, ImGuiInputTextFlags_EnterReturnsTrue);
  std::string n = a.layoutName;
  bool ok = !n.empty() && std::all_of(n.begin(), n.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' '; });
  if (!n.empty() && !ok) { ImGui::PushStyleColor(ImGuiCol_Text, brandToken("danger")); ImGui::TextUnformatted("Letters, digits, space, '-' and '_' only."); ImGui::PopStyleColor(); }
  if (ImGui::Button("Cancel", ImVec2(96, 0))) ImGui::CloseCurrentPopup();
  ImGui::SameLine();
  ImGui::BeginDisabled(!ok);
  if (primaryButton("Save", ImVec2(96, 0)) || (enter && ok)) {
    fs::path dir = a.project.root / "Library" / "Editor" / "layouts";
    std::error_code ec;
    fs::create_directories(dir, ec);
    ImGui::SaveIniSettingsToDisk((dir / (n + ".ini")).string().c_str());
    EditorLog::instance().info("layout saved: " + n);
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  ImGui::EndPopup();
}

}  // namespace editor
}  // namespace rensga
