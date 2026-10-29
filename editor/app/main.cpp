// rensga-editor: SDL2 + OpenGL 3.3 + Dear ImGui (docking). Brand theme, Unity-style default layout.
// Usage: rensga-editor [--project <dir>] [--play] [--quit-after N]
//                      [--no-auto-rebuild | --auto-rebuild] [--restore-state]
//                      [--screenshot <file.png> [--screenshot-frames N] [--select <object name>] [--focus <window>] [--ui-event click|rclick|move:x,y ...]]
#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include <imgui_internal.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "app.h"
#include "rensga/editor/editor_log.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;

bool App::openProject(const std::string& dir) {
  EditorLog& log = EditorLog::instance();
  Project p;
  std::string err;
  if (!p.load(dir, err)) { log.error("project load failed: " + err); return false; }
  project = std::move(p);
  loaded = true;
  if (!project.target.empty() && TargetRegistry::find(project.target)) target = project.target;
  log.info("project loaded: " + project.name + " (" + std::to_string(project.scenes.size()) + " scene(s))");
  doc = SceneDocument();
  if (!project.scenes.empty()) {
    if (!doc.load(project.scenes[0].path, err)) log.error("scene load failed: " + err);
    else for (const SceneObject& o : doc.scene().objects)
      log.info("  object " + o.name + " [" + o.guid + "] components=" + std::to_string(o.components.size()));
  }
  assetsStamp = -100;
  meshes.setProject(project.root);
  sv = SceneViewState();
  if (persistLayout && sv.cam.load(cameraFile())) sv.needFrame = false;
  scripts.scanStamp = -100;
  scripts.list.clear();
  // Layout persists per project; a project without one gets the default.
  layoutIni = project.root / "Library" / "Editor" / "layout.ini";
  std::error_code ec;
  if (persistLayout && fs::exists(layoutIni, ec)) ImGui::LoadIniSettingsFromDisk(layoutIni.string().c_str());
  else resetLayout = true;
  scriptsSync(*this);  // sources changed since the last build? rebuild in the background
  return true;
}

static void saveLayout(App& a) {
  if (!a.persistLayout || !a.loaded) return;
  std::error_code ec;
  fs::create_directories(a.layoutIni.parent_path(), ec);
  ImGui::SaveIniSettingsToDisk(a.layoutIni.string().c_str());
}

int main(int argc, char** argv) {
  std::string projectDir, shotFile, selectName, focusName;
  std::vector<std::string> uiEvents;  // screenshot tooling: click|rclick|move:x,y, one every 6 frames from frame 6
  bool autoPlay = false, restoreState = false;
  int autoRebuild = -1;  // -1 default (on, except in screenshot mode), 0 off, 1 on
  double quitAfter = -1;
  int shotFrames = 30;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--project") && i + 1 < argc) projectDir = argv[++i];
    else if (!std::strcmp(argv[i], "--play")) autoPlay = true;
    else if (!std::strcmp(argv[i], "--restore-state")) restoreState = true;
    else if (!std::strcmp(argv[i], "--no-auto-rebuild")) autoRebuild = 0;
    else if (!std::strcmp(argv[i], "--auto-rebuild")) autoRebuild = 1;
    else if (!std::strcmp(argv[i], "--quit-after") && i + 1 < argc) quitAfter = std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shotFile = argv[++i];
    else if (!std::strcmp(argv[i], "--screenshot-frames") && i + 1 < argc) shotFrames = std::atoi(argv[++i]);
    else if ((!std::strcmp(argv[i], "--focus") || !std::strcmp(argv[i], "--tab")) && i + 1 < argc) focusName = argv[++i];
    else if (!std::strcmp(argv[i], "--select") && i + 1 < argc) selectName = argv[++i];
    else if (!std::strcmp(argv[i], "--ui-event") && i + 1 < argc) uiEvents.push_back(argv[++i]);
    else {
      std::fprintf(stderr, "usage: rensga-editor [--project <dir>] [--play] [--quit-after N] [--no-auto-rebuild | --auto-rebuild] [--restore-state] "
                           "[--screenshot <file.png> [--screenshot-frames N] [--select <object name>] [--focus <window> | --tab <window>] [--ui-event click|rclick|move:x,y ...]]\n");
      return 2;
    }
  }
  const bool shotMode = !shotFile.empty();
  // The binary to re-execute after a script rebuild, resolved now (the rebuild replaces the file).
  std::error_code selfEc;
  const fs::path selfExe = fs::read_symlink("/proc/self/exe", selfEc);
  std::vector<std::string> restartArgs;
  for (int i = 0; i < argc; ++i) restartArgs.push_back(argv[i]);
  if (!restoreState) restartArgs.push_back("--restore-state");

  App app;
  EditorLog& log = EditorLog::instance();
  log.info("rensga-editor started");
  for (TargetModule* m : TargetRegistry::all()) log.info("target available: " + std::string(m->id()));
  if (!TargetRegistry::all().empty()) app.target = std::string(TargetRegistry::all()[0]->id());
  loadBrand();

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  const int winW = shotMode ? 1600 : 1440, winH = shotMode ? 900 : 860;
  SDL_Window* window = SDL_CreateWindow("Rensga Editor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH,
                                        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) { std::fprintf(stderr, "window failed: %s\n", SDL_GetError()); return 1; }
  if (SDL_Surface* icon = makePequiIcon(64)) { SDL_SetWindowIcon(window, icon); SDL_FreeSurface(icon); }
  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (!gl) { std::fprintf(stderr, "GL 3.3 context failed: %s\n", SDL_GetError()); return 1; }
  SDL_GL_MakeCurrent(window, gl);
  SDL_GL_SetSwapInterval(shotMode ? 0 : 1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigDockingTransparentPayload = true;
  io.IniFilename = nullptr;  // layout is saved by hand to <project>/Library/Editor/layout.ini
  applyBrandTheme();
  app.fonts = loadBrandFonts();
  ImGui_ImplSDL2_InitForOpenGL(window, gl);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  if (!app.renderer.init()) log.error("scene renderer failed to initialise");
  app.persistLayout = !shotMode;
#if defined(RENSGA_EDITOR_BUILD_DIR) && defined(RENSGA_EDITOR_PROJECT_DIR)
  app.scripts.buildDir = RENSGA_EDITOR_BUILD_DIR;
  app.scripts.compiledProject = RENSGA_EDITOR_PROJECT_DIR;
  app.scripts.cmake = RENSGA_EDITOR_CMAKE;
  app.scripts.enabled = autoRebuild == 1 || (autoRebuild == -1 && !shotMode && !selfEc);
#endif
  if (!projectDir.empty()) {
    app.openProject(projectDir);
    if (restoreState) {
      restoreReloadState(app);
      if (restoreState && app.doc.loaded() && !app.restoreFocus.empty()) focusName = app.restoreFocus;
    }
    if (shotMode) app.resetLayout = true;
    if (!selectName.empty()) {
      for (const SceneObject& o : app.doc.scene().objects)
        if (o.name == selectName) { app.doc.select(o.guid); break; }
      if (app.doc.selected().empty()) log.warn("--select: no object named " + selectName);
    }
  }

  Uint64 start = SDL_GetPerformanceCounter();
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
  bool running = true, playTriggered = false, restartNow = false;
  std::string focusedPanel;
  int exitCode = 0, frame = 0;
  std::string lastTitle;
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL2_ProcessEvent(&ev);
      if (ev.type == SDL_QUIT) running = false;
      if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) scriptsOnFocus(app);
    }
    tickScripts(app);
    if (app.scripts.restartRequested && !restartNow) {
      restartNow = true;
      app.play.stop();
      saveLayout(app);
      saveReloadState(app, focusedPanel, app.scripts.playAfter || app.restoredPlayAfter);
      running = false;
    }
    double t = static_cast<double>(SDL_GetPerformanceCounter() - start) / freq;
    if (autoPlay && !playTriggered && t > 0.3) {
      playTriggered = true;
      doPlay(app);
      // Idle after doPlay: it could not start, unless Play is waiting for the script rebuild (the restart resumes it)
      if (app.play.status() == PlayStatus::Idle && !app.scripts.builder.busy()) { exitCode = 1; running = false; }
    }
    if (autoPlay && playTriggered && !app.scripts.builder.busy() && app.scripts.failed) {
      log.error("play: script compile failed");
      exitCode = 1;
      running = false;
    }
    if (restoreState && !playTriggered && app.restoredPlayAfter && !autoPlay && t > 0.3) {
      playTriggered = true;  // Play was waiting for the rebuild: resume it
      app.restoredPlayAfter = false;
      doPlay(app);
    }
    if (quitAfter >= 0 && t >= quitAfter) running = false;
    if (app.quitRequested) running = false;
    app.play.pump();
    if (autoPlay && playTriggered && app.play.finished() && !app.play.lastOk()) {
      log.error("play: the pipeline failed");
      std::fprintf(stderr, "rensga-editor: --play pipeline failed\n");
      exitCode = 1;
      running = false;
    }
    if (!app.pendingProject.empty()) {
      saveLayout(app);
      app.saveCamera();
      std::string dir = std::move(app.pendingProject);
      app.pendingProject.clear();
      app.openProject(dir);
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    if (!restoreState && frame >= 6 && (frame - 6) % 6 == 0 && static_cast<size_t>((frame - 6) / 6) < uiEvents.size()) {
      const std::string& e = uiEvents[(frame - 6) / 6];
      float x = 0, y = 0;
      size_t c = e.find(':');
      const std::string act = e.substr(0, c), arg = c == std::string::npos ? std::string() : e.substr(c + 1);
      // test hooks that run the same code paths as the UI
      if (act == "script") createScriptAction(app, arg);                       // Create > C++ Script
      else if (act == "focus") scriptsOnFocus(app);                            // window focus gained
      else if (act == "save") app.saveScene();
      else if (act == "play") doPlay(app);
      else if (act == "create") createPrimitiveObject(app, arg, "");           // GameObject > 3D Object > <shape>
      else if (act == "addscript") addScriptToObject(app, app.doc.selected(), arg);  // drop a script on the selection
      else if (act == "select") {
        for (const SceneObject& o : app.doc.scene().objects) if (o.name == arg) { app.doc.select(o.guid); break; }
      } else if (c != std::string::npos && std::sscanf(arg.c_str(), "%f,%f", &x, &y) == 2) {
        io.AddMousePosEvent(x, y);
        if (act == "click" || act == "rclick") {
          int b = act == "click" ? 0 : 1;
          io.AddMouseButtonEvent(b, true);
          io.AddMouseButtonEvent(b, false);
        }
      }
    }
    if (!focusName.empty() && frame == 3 && !shotMode) ImGui::SetWindowFocus(focusName.c_str());
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    drawMenuBar(app);
    drawToolbar(app);
    drawDockSpaceAndLayout(app);
    drawHierarchy(app);
    drawSceneView(app);
    drawGameView(app);
    drawInspector(app);
    drawProjectPanel(app);
    drawConsole(app);
    drawDialogs(app);
    if (shotMode && !focusName.empty() && frame == shotFrames / 2) ImGui::SetWindowFocus(focusName.c_str());
    if (ImGuiWindow* nw = ImGui::GetCurrentContext()->NavWindow) {
      static const char* const kPanels[] = {"Hierarchy", "Inspector", "Project", "Console", "Scene", "Game"};
      for (const char* pn : kPanels) if (!std::strcmp(nw->RootWindow->Name, pn)) focusedPanel = pn;
    }
    ImGui::Render();

    int w, h;
    SDL_GL_GetDrawableSize(window, &w, &h);
    glViewport(0, 0, w, h);
    ImVec4 bg = brandToken("bg");
    glClearColor(bg.x, bg.y, bg.z, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ++frame;
    if (shotMode && frame >= shotFrames) {
      if (writeScreenshotPng(shotFile.c_str(), w, h)) std::printf("screenshot: %s (%dx%d, %d frames)\n", shotFile.c_str(), w, h, frame);
      else { std::fprintf(stderr, "screenshot failed: %s\n", shotFile.c_str()); exitCode = 1; }
      running = false;
    }
    SDL_GL_SwapWindow(window);

    if (io.WantSaveIniSettings) { saveLayout(app); io.WantSaveIniSettings = false; }
    std::string title = "Rensga Editor";
    if (app.loaded) title += " - " + app.project.name;
    if (app.doc.loaded()) title += " - " + app.doc.path().stem().string() + (app.doc.dirty() ? "*" : "");
    if (title != lastTitle) { SDL_SetWindowTitle(window, title.c_str()); lastTitle = title; }
  }

  if (!restartNow) saveLayout(app);
  app.saveCamera();
  app.play.stop();
  app.play.pump();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  if (restartNow) {  // the rebuilt editor takes over this process; the saved state brings the session back
    std::vector<char*> av;
    for (std::string& a : restartArgs) av.push_back(a.data());
    av.push_back(nullptr);
    std::fflush(nullptr);
    execv(selfExe.c_str(), av.data());
    std::fprintf(stderr, "rensga-editor: restart failed (%s)\n", std::strerror(errno));
    return 1;
  }
  return exitCode;
}
