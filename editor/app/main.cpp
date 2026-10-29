// rensga-editor: SDL2 + OpenGL 3.3 + Dear ImGui (docking). MVP: Hierarchy, Inspector, Console, Play.
// Usage: rensga-editor [--project <dir>] [--play] [--quit-after N]
#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "rensga/core/reflect.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/play_controller.h"
#include "rensga/editor/project.h"
#include "rensga/editor/target_module.h"

using namespace rensga;
using namespace rensga::editor;

namespace {

struct App {
  Project project;
  bool loaded = false;
  PlayController play;
  std::string target;
  int selected = -1;  // object index in scene 0
  bool showInfo = true, showWarn = true, showError = true, showEditor = true, showGame = true;
  bool scrollLog = true;
};

// The single entry point for Play: the toolbar button and --play both call it.
void doPlay(App& a) {
  if (!a.loaded) { EditorLog::instance().error("play: no project loaded"); return; }
  if (a.target.empty()) { EditorLog::instance().error("play: no target registered"); return; }
  if (!a.play.play(a.project.root, a.target)) EditorLog::instance().warn("play: already running");
}

void drawToolbar(App& a) {
  ImGui::Begin("Toolbar");
  bool busy = a.play.busy();
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Play")) doPlay(a);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::ArrowButton("##playmenu", ImGuiDir_Down)) ImGui::OpenPopup("playmode");
  if (ImGui::BeginPopup("playmode")) {
    if (ImGui::MenuItem("Play on console", nullptr, true)) {}
    ImGui::BeginDisabled();
    ImGui::MenuItem("Play in editor");
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("arrives in Editor 5");
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!busy);
  if (ImGui::Button("Stop")) a.play.stop();
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::SetNextItemWidth(180);
  if (ImGui::BeginCombo("Target", a.target.c_str())) {
    for (TargetModule* m : TargetRegistry::all()) {
      std::string id(m->id());
      if (ImGui::Selectable((std::string(m->displayName()) + "##" + id).c_str(), id == a.target)) a.target = id;
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::Text("Status: %s", playStatusName(a.play.status()));
  ImGui::End();
}

void drawHierarchy(App& a) {
  ImGui::Begin("Hierarchy");
  if (!a.loaded || a.project.scenes.empty()) {
    ImGui::TextDisabled("No scene loaded");
  } else {
    const Scene& s = a.project.scenes[0];
    for (size_t i = 0; i < s.objects.size(); ++i) {
      const SceneObject& o = s.objects[i];
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Selectable(o.name.c_str(), a.selected == static_cast<int>(i))) a.selected = static_cast<int>(i);
      ImGui::PopID();
    }
  }
  ImGui::End();
}

void drawFieldJson(const char* name, const nlohmann::json& v) {
  std::string t = v.dump();
  ImGui::BeginDisabled();
  ImGui::LabelText(name, "%s", t.c_str());
  ImGui::EndDisabled();
}

void drawInspector(App& a) {
  ImGui::Begin("Inspector");
  if (a.loaded && !a.project.scenes.empty() && a.selected >= 0 &&
      a.selected < static_cast<int>(a.project.scenes[0].objects.size())) {
    const SceneObject& o = a.project.scenes[0].objects[a.selected];
    ImGui::Text("%s%s", o.name.c_str(), o.active ? "" : " (inactive)");
    ImGui::TextDisabled("%s", o.guid.c_str());
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::BeginDisabled();
      float p[3] = {o.position[0], o.position[1], o.position[2]};
      float r[4] = {o.rotation[0], o.rotation[1], o.rotation[2], o.rotation[3]};
      float s[3] = {o.scale[0], o.scale[1], o.scale[2]};
      ImGui::DragFloat3("Position", p);
      ImGui::DragFloat4("Rotation", r);
      ImGui::DragFloat3("Scale", s);
      ImGui::EndDisabled();
    }
    for (const SceneComponent& c : o.components) {
      if (!ImGui::CollapsingHeader(c.type.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;
      ImGui::PushID(c.type.c_str());
      const TypeInfo* ti = TypeRegistry::instance().find(c.type.c_str());
      if (ti) {  // draw from reflection FieldDescs
        for (u32 i = 0; i < ti->fieldCount; ++i) {
          const FieldDesc& f = ti->fields[i];
          if (f.header) ImGui::SeparatorText(f.header);
          std::string label = std::string(f.name) + " (" + fieldTypeName(f.type) + ")";
          if (f.hasRange) label += " [" + std::to_string(f.rangeMin) + ".." + std::to_string(f.rangeMax) + "]";
          if (c.fields.contains(f.name)) drawFieldJson(label.c_str(), c.fields[f.name]);
          else drawFieldJson(label.c_str(), "(default)");
        }
      } else {  // type unknown to the editor (e.g. a project script): show the raw scene data
        for (auto it = c.fields.begin(); it != c.fields.end(); ++it) drawFieldJson(it.key().c_str(), it.value());
      }
      ImGui::PopID();
    }
  } else {
    ImGui::TextDisabled("Select an object");
  }
  ImGui::End();
}

void drawConsole(App& a) {
  ImGui::Begin("Console");
  if (ImGui::Button("Clear")) {}  // log is append-only in the MVP
  ImGui::SameLine(); ImGui::Checkbox("Info", &a.showInfo);
  ImGui::SameLine(); ImGui::Checkbox("Warn", &a.showWarn);
  ImGui::SameLine(); ImGui::Checkbox("Error", &a.showError);
  ImGui::SameLine(); ImGui::Checkbox("Editor", &a.showEditor);
  ImGui::SameLine(); ImGui::Checkbox("Game", &a.showGame);
  ImGui::Separator();
  ImGui::BeginChild("lines", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
  for (const LogLine& l : EditorLog::instance().snapshot()) {
    if (l.level == LogLevel::Info && !a.showInfo) continue;
    if (l.level == LogLevel::Warn && !a.showWarn) continue;
    if (l.level == LogLevel::Error && !a.showError) continue;
    if (l.source == LogSource::Editor && !a.showEditor) continue;
    if (l.source == LogSource::Game && !a.showGame) continue;
    ImVec4 col = l.level == LogLevel::Error ? ImVec4(1, 0.4f, 0.4f, 1)
               : l.level == LogLevel::Warn ? ImVec4(1, 0.8f, 0.3f, 1) : ImVec4(0.85f, 0.85f, 0.85f, 1);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::Text("%s %s", l.source == LogSource::Game ? "[game]" : "[editor]", l.text.c_str());
    ImGui::PopStyleColor();
  }
  if (a.scrollLog && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace

int main(int argc, char** argv) {
  std::string projectDir;
  bool autoPlay = false;
  double quitAfter = -1;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--project") && i + 1 < argc) projectDir = argv[++i];
    else if (!std::strcmp(argv[i], "--play")) autoPlay = true;
    else if (!std::strcmp(argv[i], "--quit-after") && i + 1 < argc) quitAfter = std::atof(argv[++i]);
    else { std::fprintf(stderr, "usage: rensga-editor [--project <dir>] [--play] [--quit-after N]\n"); return 2; }
  }

  App app;
  EditorLog& log = EditorLog::instance();
  log.info("rensga-editor started");
  for (TargetModule* m : TargetRegistry::all()) log.info("target available: " + std::string(m->id()));
  if (!TargetRegistry::all().empty()) app.target = std::string(TargetRegistry::all()[0]->id());

  if (!projectDir.empty()) {
    std::string err;
    if (app.project.load(projectDir, err)) {
      app.loaded = true;
      if (!app.project.target.empty() && TargetRegistry::find(app.project.target)) app.target = app.project.target;
      log.info("project loaded: " + app.project.name + " (" + std::to_string(app.project.scenes.size()) + " scene(s))");
      for (const Scene& s : app.project.scenes) {
        log.info("scene " + s.guid + ": " + std::to_string(s.objects.size()) + " objects");
        for (const SceneObject& o : s.objects)
          log.info("  object " + o.name + " [" + o.guid + "] components=" + std::to_string(o.components.size()));
      }
    } else {
      log.error("project load failed: " + err);
    }
  }

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_Window* window = SDL_CreateWindow("Rensga Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) { std::fprintf(stderr, "window failed: %s\n", SDL_GetError()); return 1; }
  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (!gl) { std::fprintf(stderr, "GL 3.3 context failed: %s\n", SDL_GetError()); return 1; }
  SDL_GL_MakeCurrent(window, gl);
  SDL_GL_SetSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForOpenGL(window, gl);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  Uint64 start = SDL_GetPerformanceCounter();
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
  bool running = true, playTriggered = false;
  int exitCode = 0;
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL2_ProcessEvent(&ev);
      if (ev.type == SDL_QUIT) running = false;
    }
    double t = static_cast<double>(SDL_GetPerformanceCounter() - start) / freq;
    if (autoPlay && !playTriggered && t > 0.3) { playTriggered = true; doPlay(app); }
    if (quitAfter >= 0 && t >= quitAfter) running = false;
    app.play.pump();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    drawToolbar(app);
    drawHierarchy(app);
    drawInspector(app);
    drawConsole(app);
    ImGui::Render();

    int w, h;
    SDL_GL_GetDrawableSize(window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.1f, 0.12f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
  }

  app.play.stop();
  app.play.pump();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return exitCode;
}
