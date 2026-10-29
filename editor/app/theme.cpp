#include "theme.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/editor/editor_log.h"

extern const char* const kBrandPaletteJson;

namespace rensga {
namespace editor {

namespace {

struct Brand {
  std::map<std::string, ImVec4> token, ramp, icon;
  std::vector<std::pair<std::string, ImVec4>> editor;
  std::map<std::string, std::pair<ImVec4, std::string>> targets;  // key -> color, label
  bool ok = false;
} g_brand;

bool parseHex(const std::string& h, ImVec4& out) {
  if (h.size() != 7 || h[0] != '#') return false;
  unsigned v = static_cast<unsigned>(std::strtoul(h.c_str() + 1, nullptr, 16));
  out = ImVec4(((v >> 16) & 255) / 255.0f, ((v >> 8) & 255) / 255.0f, (v & 255) / 255.0f, 1.0f);
  return true;
}

ImVec4 fallback() { return ImVec4(1, 0, 1, 1); }  // loud magenta if a token is missing

ImU32 u32(const std::map<std::string, ImVec4>& m, const char* k, float alpha = 1.0f) {
  auto it = m.find(k);
  ImVec4 c = it == m.end() ? fallback() : it->second;
  c.w *= alpha;
  return ImGui::ColorConvertFloat4ToU32(c);
}

// ---- pequi geometry in the 64 x 64 icon space (from the brand icon SVG) ----
using P2 = ImVec2;
void bezier(std::vector<P2>& out, P2 a, P2 b, P2 c, P2 d, int n = 12) {
  for (int i = 1; i <= n; ++i) {
    float t = static_cast<float>(i) / n, u = 1 - t;
    out.push_back(P2(u * u * u * a.x + 3 * u * u * t * b.x + 3 * u * t * t * c.x + t * t * t * d.x,
                     u * u * u * a.y + 3 * u * u * t * b.y + 3 * u * t * t * c.y + t * t * t * d.y));
  }
}
const std::vector<P2>& stemPts() {
  static std::vector<P2> p = [] { std::vector<P2> v{P2(33, 14)}; bezier(v, P2(33, 14), P2(33, 10), P2(34, 7), P2(36, 5)); return v; }();
  return p;
}
const std::vector<P2>& leafPts() {
  static std::vector<P2> p = [] {
    std::vector<P2> v{P2(35, 10)};
    bezier(v, P2(35, 10), P2(41, 4), P2(50, 4), P2(54, 8));
    bezier(v, P2(54, 8), P2(49, 13), P2(41, 14), P2(35, 10));
    v.pop_back();
    return v;
  }();
  return p;
}

bool inPoly(const std::vector<P2>& poly, float x, float y) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
    if ((poly[i].y > y) != (poly[j].y > y) &&
        x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
      in = !in;
  return in;
}

float distSeg(P2 p, P2 a, P2 b) {
  float vx = b.x - a.x, vy = b.y - a.y, wx = p.x - a.x, wy = p.y - a.y;
  float l2 = vx * vx + vy * vy, t = l2 > 0 ? (wx * vx + wy * vy) / l2 : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  float dx = p.x - (a.x + t * vx), dy = p.y - (a.y + t * vy);
  return std::sqrt(dx * dx + dy * dy);
}

// Painter's-order sample of the icon at (x, y) in 64-space. Returns straight RGBA.
ImVec4 samplePequi(float x, float y) {
  ImVec4 col(0, 0, 0, 0);
  auto paint = [&](const char* part, float a = 1.0f) {
    auto it = g_brand.icon.find(part);
    ImVec4 c = it == g_brand.icon.end() ? fallback() : it->second;
    float oa = a + col.w * (1 - a);
    if (oa <= 0) return;
    col = ImVec4((c.x * a + col.x * col.w * (1 - a)) / oa, (c.y * a + col.y * col.w * (1 - a)) / oa,
                 (c.z * a + col.z * col.w * (1 - a)) / oa, oa);
  };
  const std::vector<P2>& st = stemPts();
  for (size_t i = 1; i < st.size(); ++i)
    if (distSeg(P2(x, y), st[i - 1], st[i]) <= 1.25f) { paint("Stem"); break; }
  if (inPoly(leafPts(), x, y)) paint("Leaf");
  float dh = std::sqrt((x - 32) * (x - 32) + (y - 37) * (y - 37));
  if (dh <= 24) paint(dh >= 22 ? "Husk rim" : "Husk");
  float dp = std::sqrt((x - 32) * (x - 32) + (y - 38) * (y - 38));
  if (dp <= 15.75f) paint(dp >= 14.25f ? "Pulp rim" : "Pulp");
  float ex = (x - 27) / 5, ey = (y - 33) / 3.5f;
  if (ex * ex + ey * ey <= 1) paint("Highlight", 0.85f);
  return col;
}

}  // namespace

bool loadBrand() {
  using nlohmann::json;
  json j = json::parse(kBrandPaletteJson, nullptr, false);
  if (j.is_discarded()) { EditorLog::instance().error("brand: embedded palette.json is not valid JSON"); return false; }
  Brand b;
  for (const json& t : j.value("tokens", json::array())) {
    ImVec4 c;
    if (parseHex(t.value("dark", ""), c)) b.token[t.value("name", "")] = c;
  }
  for (const json& r : j.value("ramps", json::array()))
    for (const json& s : r.value("steps", json::array())) {
      ImVec4 c;
      if (parseHex(s.value("hex", ""), c)) b.ramp[r.value("key", "") + "." + std::to_string(s.value("step", 0))] = c;
    }
  for (const json& i : j.value("icon", json::array())) {
    ImVec4 c;
    if (parseHex(i.value("hex", ""), c)) b.icon[i.value("part", "")] = c;
  }
  for (const json& e : j.value("editor", json::array())) {
    ImVec4 c;
    if (parseHex(e.value("hex", ""), c)) b.editor.emplace_back(e.value("imgui", ""), c);
  }
  for (const json& t : j.value("targets", json::array())) {
    ImVec4 c;
    if (parseHex(t.value("hex", ""), c)) b.targets[t.value("key", "")] = {c, t.value("target", "")};
  }
  b.ok = !b.token.empty() && !b.editor.empty();
  g_brand = std::move(b);
  if (!g_brand.ok) EditorLog::instance().error("brand: palette.json has no tokens or editor mapping");
  return g_brand.ok;
}

ImVec4 brandToken(const char* name) {
  auto it = g_brand.token.find(name);
  return it == g_brand.token.end() ? fallback() : it->second;
}

bool brandTarget(const std::string& key, ImVec4& color, std::string& label) {
  auto it = g_brand.targets.find(key);
  if (it == g_brand.targets.end()) return false;
  color = it->second.first;
  label = it->second.second;
  return true;
}

ImVec4 brandRamp(const char* ref) {
  auto it = g_brand.ramp.find(ref);
  return it == g_brand.ramp.end() ? fallback() : it->second;
}

void applyBrandTheme() {
  ImGuiStyle& st = ImGui::GetStyle();
  ImGui::StyleColorsDark(&st);  // base for anything the brand mapping leaves out
  if (g_brand.ok) {
    for (const auto& e : g_brand.editor) {
      int idx = -1;
      for (int i = 0; i < ImGuiCol_COUNT; ++i)
        if (e.first == ImGui::GetStyleColorName(i)) { idx = i; break; }
      if (idx < 0) { EditorLog::instance().warn("brand: unknown ImGui color " + e.first); continue; }
      st.Colors[idx] = e.second;
    }
    // The brand notes ask for about 40% on these; dims for modals and window switching.
    st.Colors[ImGuiCol_DockingPreview].w = 0.4f;
    st.Colors[ImGuiCol_TextSelectedBg].w = 0.6f;
    st.Colors[ImGuiCol_ModalWindowDimBg].w = 0.6f;
    st.Colors[ImGuiCol_NavWindowingDimBg].w = 0.5f;
    st.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
  }
  // Metrics: windows 8, frames 6 (buttons push 8 where drawn), 4 px grid spacing.
  st.WindowRounding = 8; st.ChildRounding = 6; st.PopupRounding = 8; st.FrameRounding = 6;
  st.GrabRounding = 6; st.ScrollbarRounding = 8; st.TabRounding = 6;
  st.WindowPadding = ImVec2(10, 10); st.FramePadding = ImVec2(8, 5);
  st.ItemSpacing = ImVec2(8, 6); st.ItemInnerSpacing = ImVec2(6, 4);
  st.IndentSpacing = 16; st.ScrollbarSize = 12; st.GrabMinSize = 10;
  st.WindowBorderSize = 1; st.ChildBorderSize = 1; st.PopupBorderSize = 1; st.FrameBorderSize = 0;
  st.TabBorderSize = 0; st.TabBarBorderSize = 1; st.SeparatorTextBorderSize = 1;
  st.WindowMenuButtonPosition = ImGuiDir_None;
  st.DockingSeparatorSize = 2;
  st.FontSizeBase = 15.0f;
}

Fonts loadBrandFonts() {
  namespace fs = std::filesystem;
  Fonts f;
  ImGuiIO& io = ImGui::GetIO();
  fs::path dir = RENSGA_FONT_DIR;
  auto load = [&](const char* file, float size) -> ImFont* {
    fs::path p = dir / file;
    if (!fs::exists(p)) { EditorLog::instance().warn("font missing, using the ImGui default: " + p.string()); return nullptr; }
    return io.Fonts->AddFontFromFileTTF(p.string().c_str(), size);
  };
  f.ui = load("Inter-Regular.ttf", f.uiSize);
  f.uiBold = load("Inter-SemiBold.ttf", f.uiSize);
  f.mono = load("JetBrainsMono-Regular.ttf", f.monoSize);
  if (!f.ui) f.ui = io.Fonts->AddFontDefault();
  if (!f.uiBold) f.uiBold = f.ui;
  if (!f.mono) f.mono = f.ui;
  io.FontDefault = f.ui;
  return f;
}

void drawPequi(ImDrawList* dl, ImVec2 o, float size) {
  const float k = size / 64.0f;
  auto tf = [&](P2 p) { return ImVec2(o.x + p.x * k, o.y + p.y * k); };
  const auto& m = g_brand.icon;
  std::vector<P2> pts;
  for (const P2& p : stemPts()) pts.push_back(tf(p));
  dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), u32(m, "Stem"), ImDrawFlags_None, 2.5f * k);
  pts.clear();
  for (const P2& p : leafPts()) pts.push_back(tf(p));
  dl->AddConvexPolyFilled(pts.data(), static_cast<int>(pts.size()), u32(m, "Leaf"));
  dl->AddCircleFilled(tf(P2(32, 37)), 23 * k, u32(m, "Husk"), 48);
  dl->AddCircle(tf(P2(32, 37)), 23 * k, u32(m, "Husk rim"), 48, 2 * k);
  dl->AddCircleFilled(tf(P2(32, 38)), 15 * k, u32(m, "Pulp"), 40);
  dl->AddCircle(tf(P2(32, 38)), 15 * k, u32(m, "Pulp rim"), 40, 1.5f * k);
  dl->AddEllipseFilled(tf(P2(27, 33)), ImVec2(5 * k, 3.5f * k), u32(m, "Highlight", 0.85f), 0, 20);
}

SDL_Surface* makePequiIcon(int size) {
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_RGBA32);
  if (!s) return nullptr;
  const int ss = 4;
  for (int py = 0; py < size; ++py) {
    Uint8* row = static_cast<Uint8*>(s->pixels) + py * s->pitch;
    for (int px = 0; px < size; ++px) {
      float r = 0, g = 0, b = 0, a = 0;
      for (int sy = 0; sy < ss; ++sy)
        for (int sx = 0; sx < ss; ++sx) {
          ImVec4 c = samplePequi((px + (sx + 0.5f) / ss) * 64.0f / size, (py + (sy + 0.5f) / ss) * 64.0f / size);
          r += c.x * c.w; g += c.y * c.w; b += c.z * c.w; a += c.w;
        }
      float n = static_cast<float>(ss * ss);
      Uint8* d = row + px * 4;
      if (a > 0) { d[0] = Uint8(255 * r / a); d[1] = Uint8(255 * g / a); d[2] = Uint8(255 * b / a); }
      else d[0] = d[1] = d[2] = 0;
      d[3] = Uint8(255 * a / n);
    }
  }
  return s;
}

}  // namespace editor
}  // namespace rensga
