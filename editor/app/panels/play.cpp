// F3.04 (Editor 5): Play in editor, glue between the toolbar / menus / Game view and editor/core PlaySession.
// Also the input of the Game view (the player's keyboard and gamepad mapping, SdlPlatform::applyKeyboard / applyController),
// the editor tint while playing and the --ui-event verbs used by tests (documented in AGENTS.md).
#include <SDL.h>
#include <imgui_internal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app.h"
#include "panel_registry.h"
#include "panels/asset_ui.h"
#include "panels/tile_tools.h"
#include "rensga/core/reflect_json.h"
#include "rensga/editor/editor_log.h"
#include "rensga/platform/sdl.h"

namespace rensga {
namespace editor {

bool startPlayInEditor(App& a) {
  EditorLog& log = EditorLog::instance();
  if (!a.loaded || !a.doc.loaded()) { log.error("play: no scene is open"); return false; }
  if (a.session.active()) return true;
  if (a.play.busy()) { log.warn("play: the console run is still active; stop it first"); return false; }
  if (a.scripts.enabled) {  // changed scripts are compiled and hot reloaded first; Play resumes when the build ends
    ScriptSync sync = scriptsSync(a);
    if (sync == ScriptSync::Building) { a.scripts.playAfter = true; log.info("play: waiting for the script rebuild"); return false; }
    if (sync == ScriptSync::Failed) { log.error("play: fix script compile errors first"); return false; }
  }
  std::string err;
  if (const int n = saveDirtyTilemaps(a)) log.info("play: saved " + std::to_string(n) + " modified tilemap(s) before cooking");  // the cook reads tilemaps from disk
  a.editWorld.release();  // the edit-mode World shares the engine's global state with the session
  log.info("play: cooking '" + a.doc.path().stem().string() + "' for " + a.project.target + " (from memory)");
  if (!a.session.start(a.doc, a.project.root, a.project.target, err)) {
    log.error("play: " + err);
    return false;
  }
  EditorWorld* w = a.session.world();
  log.info("play: started, " + std::to_string(w ? w->world().objectCount() : 0) + " objects, cook " +
           std::to_string(w ? static_cast<int>(w->cookMs()) : 0) + " ms (assets cooked " + std::to_string(w ? w->cookedAssets() : 0) +
           ", cached " + std::to_string(w ? w->cachedAssets() : 0) + ")");
  std::printf("play: started frames=0 target=%s\n", a.project.target.c_str());
  return true;
}

void stopPlayInEditor(App& a) {
  if (!a.session.active()) return;
  const uint64_t frames = a.session.frames();
  const uint64_t fixedSteps = a.session.fixedSteps();
  a.session.stop(a.doc);
  gameViewRelease();
  const bool same = a.doc.document() == a.session.snapshotJson();
  std::printf("play-check: stopped frames=%llu fixed=%llu identical=%d dirty=%d dirty_before=%d undo=%zu undo_before=%zu selected_same=%d\n",
              static_cast<unsigned long long>(frames), static_cast<unsigned long long>(fixedSteps), same ? 1 : 0, a.doc.dirty() ? 1 : 0,
              a.session.snapshotDirty() ? 1 : 0, a.doc.undoDepth(), a.session.snapshotUndoDepth(),
              a.doc.selected() == a.session.snapshotSelected() ? 1 : 0);
  std::fflush(stdout);
}

// ---- Game view input ----------------------------------------------------------------------------------------------
static SDL_GameController* g_controller = nullptr;

static void openController() {
  if (g_controller) return;
  if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER) && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) return;
  for (int i = 0; i < SDL_NumJoysticks(); ++i)
    if (SDL_IsGameController(i) && (g_controller = SDL_GameControllerOpen(i))) return;
}

static VirtualGamepad readPad(App& a) {
  VirtualGamepad pad;
  unsigned char down[512] = {};
  bool any = false;
  if (!a.playKeys.empty()) {  // test hook: SDL key names, comma separated
    size_t pos = 0;
    while (pos <= a.playKeys.size()) {
      size_t e = a.playKeys.find(',', pos);
      if (e == std::string::npos) e = a.playKeys.size();
      const SDL_Scancode sc = SDL_GetScancodeFromName(a.playKeys.substr(pos, e - pos).c_str());
      if (sc != SDL_SCANCODE_UNKNOWN && sc < 512) { down[sc] = 1; any = true; }
      pos = e + 1;
    }
  } else if (a.gameFocused) {
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    for (int i = 0; i < 512; ++i) down[i] = ks[i] ? 1 : 0;
    any = true;
  }
  if (any) SdlPlatform::applyKeyboard(down, pad);
  if (a.gameFocused || !a.playKeys.empty()) {
    SDL_GameControllerUpdate();
    SdlPlatform::applyController(g_controller, pad);
  }
  return pad;
}

void tickPlayInEditor(App& a, double dt) {
  if (!a.session.active()) return;
  if (!g_controller) openController();
  a.session.tick(dt, readPad(a));
}

// ---- tint -----------------------------------------------------------------------------------------------------------------
void applyPlayTint(App& a) {
  static ImVec4 saved[ImGuiCol_COUNT];
  static bool have = false, tinted = false;
  ImGuiStyle& st = ImGui::GetStyle();
  static const ImGuiCol kCols[] = {ImGuiCol_MenuBarBg, ImGuiCol_TitleBg, ImGuiCol_TitleBgActive, ImGuiCol_Tab, ImGuiCol_TabHovered,
                                   ImGuiCol_TabSelected, ImGuiCol_TabDimmed, ImGuiCol_TabDimmedSelected, ImGuiCol_DockingEmptyBg};
  const bool on = a.session.active();
  if (on == tinted) return;
  if (on) {
    if (!have) { for (int i = 0; i < ImGuiCol_COUNT; ++i) saved[i] = st.Colors[i]; have = true; }
    const ImVec4 p = brandToken("primary");
    for (ImGuiCol c : kCols) {
      const ImVec4 b = saved[c];
      st.Colors[c] = ImVec4(b.x + (p.x - b.x) * 0.22f, b.y + (p.y - b.y) * 0.22f, b.z + (p.z - b.z) * 0.22f, b.w);
    }
  } else if (have) {
    for (ImGuiCol c : kCols) st.Colors[c] = saved[c];
  }
  tinted = on;
}

// ---- --ui-event verbs --------------------------------------------------------------------------------------------------------
static std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t p = 0;
  for (;;) {
    size_t e = s.find(sep, p);
    if (e == std::string::npos) { out.push_back(s.substr(p)); break; }
    out.push_back(s.substr(p, e - p));
    p = e + 1;
  }
  return out;
}

static const SceneObject* findByNameOrGuid(App& a, const std::string& n) {
  for (const SceneObject& o : a.doc.scene().objects) if (o.name == n || o.guid == n) return &o;
  return nullptr;
}

bool playUiHook(App& a, const std::string& act, const std::string& arg) {
  EditorLog& log = EditorLog::instance();
  if (act == "openscene") {  // openscene:<scene file name without extension>: Project window double-click
    stopPlayInEditor(a);
    for (const Scene& sc : a.project.scenes)
      if (std::filesystem::path(sc.path).stem() == arg) { openSceneFile(a, sc.path); return true; }
    log.warn("openscene: no scene named " + arg);
    return true;
  }
  if (act == "showgame") { ImGui::SetWindowFocus("Game"); return true; }  // bring the Game tab to the front
  if (act == "playedit") { if (!startPlayInEditor(a)) std::printf("play: start failed\n"); return true; }
  if (act == "stopedit") { stopPlayInEditor(a); return true; }
  if (act == "pause") { a.session.setPaused(true); return true; }
  if (act == "resume") { a.session.setPaused(false); return true; }
  if (act == "pausetoggle") { a.session.togglePause(); return true; }
  if (act == "step") {
    const int n = arg.empty() ? 1 : std::atoi(arg.c_str());
    if (a.session.state() == PlayState::Playing) a.session.setPaused(true);
    for (int i = 0; i < n; ++i) a.session.requestStep();
    return true;
  }
  if (act == "playkeys") { a.playKeys = arg == "none" ? std::string() : arg; return true; }
  if (act == "playset") {  // playset:<object>|<component index>|<field>|<json value>
    const std::vector<std::string> p = split(arg, '|');
    const SceneObject* o = p.size() == 4 ? findByNameOrGuid(a, p[0]) : nullptr;
    if (!o) { log.warn("playset: expected object|component|field|value"); return true; }
    nlohmann::json v = nlohmann::json::parse(p[3], nullptr, false);
    const bool ok = !v.is_discarded() && a.doc.setField(o->guid, std::atoi(p[1].c_str()), p[2], v);
    std::printf("playset: %s.%s -> %s (%s)\n", p[0].c_str(), p[2].c_str(), p[3].c_str(), ok ? "applied to the running World" : "refused");
    return true;
  }
  if (act == "playmove") {  // playmove:<object>|x,y,z   (local position)
    const std::vector<std::string> p = split(arg, '|');
    float v[3] = {};
    const SceneObject* o = p.size() == 2 ? findByNameOrGuid(a, p[0]) : nullptr;
    if (!o || std::sscanf(p[1].c_str(), "%f,%f,%f", &v[0], &v[1], &v[2]) != 3) { log.warn("playmove: expected object|x,y,z"); return true; }
    const bool ok = a.doc.setTransform(o->guid, v, o->rotation, o->scale);
    std::printf("playmove: %s -> %g,%g,%g (%s)\n", p[0].c_str(), v[0], v[1], v[2], ok ? "applied" : "refused");
    return true;
  }
  if (act == "playdump") {  // playdump:<object>[|<component index>|<field>]  runtime values of the World
    const std::vector<std::string> p = split(arg, '|');
    const SceneObject* o = findByNameOrGuid(a, p[0]);
    GameObject* go = o ? a.session.objectByKey(o->guid) : nullptr;
    if (!go) { std::printf("playdump: %s not found\n", p[0].c_str()); return true; }
    const Vector3 lp = go->transform.lpos_;
    const Quaternion lr = go->transform.lrot_;
    std::printf("playdump: %s pos=%.4f,%.4f,%.4f rot=%.4f,%.4f,%.4f,%.4f frames=%llu fixed=%llu", p[0].c_str(), lp.x, lp.y, lp.z,
                lr.x, lr.y, lr.z, lr.w,
                static_cast<unsigned long long>(a.session.frames()), static_cast<unsigned long long>(a.session.fixedSteps()));
    if (p.size() == 3) {
      const u32 ci = static_cast<u32>(std::atoi(p[1].c_str()));
      const TypeInfo* ti = ci < go->componentCount ? TypeRegistry::instance().byId(go->components[ci].typeId) : nullptr;
      const ComponentPool* pool = ti ? a.session.world()->world().pool(*ti) : nullptr;
      if (pool) {
        nlohmann::json f = fieldsToJson(*ti, pool->at(go->components[ci].slot));
        std::printf(" %s.%s=%s", ti->name, p[2].c_str(), f.contains(p[2]) ? f[p[2]].dump().c_str() : "?");
      }
    }
    std::printf("\n");
    std::fflush(stdout);
    return true;
  }
  return false;
}

bool playUiHolds(App& a, const std::string& event) {
  if (event.rfind("playwait:", 0) != 0) return false;
  const uint64_t n = static_cast<uint64_t>(std::atoll(event.c_str() + 9));
  return a.session.active() && a.session.state() == PlayState::Playing && a.session.frames() < n;
}

// F6 E0: panel registry (editor/app/panel_registry.h).
RENSGA_EDITOR_PANEL(play_verbs, "Play in editor (verbs)", nullptr, playUiHook, playUiHolds, 0, 70, ::rensga::editor::PanelDraw::None)

}  // namespace editor
}  // namespace rensga
