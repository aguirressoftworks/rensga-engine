// rensga-editor: SDL2 + OpenGL 3.3 + Dear ImGui (docking). Brand theme, Unity-style default layout.
// Usage: rensga-editor [--project <dir>] [--play] [--quit-after N]
//                      [--no-splash] [--splash-only] [--config-dir <dir>]
//                      [--no-auto-rebuild | --auto-rebuild]
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
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "app.h"
#include "panels/asset_ui.h"
#include "rensga/editor/editor_log.h"
#include "rensga/editor/game_module.h"

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
  else if (!project.target.empty()) {  // F3.02: a target without a TargetModule (PS2 until F8.07): edit and cook generically, no console run
    target.clear();
    log.info("target '" + project.target + "' has no editor module: console build and run are disabled");
  }
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
  scriptsOnProjectOpen(*this);  // module current? load it, else build it in the background
  // Headless runs (screenshots, --ui-event, --no-auto-rebuild) have no background job: build and load here.
  if (!scripts.enabled) ensureGameModule(project.root, true);
  return true;
}

static void modeSize(Mode m, bool shot, int& w, int& h) {
  if (m == Mode::Splash) { w = 760; h = 440; }
  else if (m == Mode::Picker) { w = 1000; h = 640; }
  else { w = shot ? 1600 : 1440; h = shot ? 900 : 860; }
}

// One window serves all three modes: splash is borderless and fixed, the others are normal windows.
static void applyMode(SDL_Window* w, Mode m, bool shot) {
  int ww, hh;
  modeSize(m, shot, ww, hh);
  SDL_SetWindowBordered(w, m == Mode::Splash ? SDL_FALSE : SDL_TRUE);
  SDL_SetWindowResizable(w, m == Mode::Splash ? SDL_FALSE : SDL_TRUE);
  SDL_SetWindowSize(w, ww, hh);
  SDL_SetWindowPosition(w, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

static void saveLayout(App& a) {
  if (!a.persistLayout || !a.loaded) return;
  std::error_code ec;
  fs::create_directories(a.layoutIni.parent_path(), ec);
  ImGui::SaveIniSettingsToDisk(a.layoutIni.string().c_str());
}

int main(int argc, char** argv) {
  std::string projectDir, shotFile, selectName, focusName, configDir;
  bool noSplash = false, splashOnly = false;
  std::vector<std::string> uiEvents;  // screenshot tooling: click|rclick|move:x,y, one every 6 frames from frame 6
  bool autoPlay = false;
  int autoRebuild = -1;  // -1 default (on, except in screenshot mode), 0 off, 1 on
  double quitAfter = -1;
  int shotFrames = 30;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--project") && i + 1 < argc) projectDir = argv[++i];
    else if (!std::strcmp(argv[i], "--no-splash")) noSplash = true;
    else if (!std::strcmp(argv[i], "--splash-only")) splashOnly = true;
    else if (!std::strcmp(argv[i], "--config-dir") && i + 1 < argc) configDir = argv[++i];
    else if (!std::strcmp(argv[i], "--play")) autoPlay = true;
    else if (!std::strcmp(argv[i], "--no-auto-rebuild")) autoRebuild = 0;
    else if (!std::strcmp(argv[i], "--auto-rebuild")) autoRebuild = 1;
    else if (!std::strcmp(argv[i], "--quit-after") && i + 1 < argc) quitAfter = std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) shotFile = argv[++i];
    else if (!std::strcmp(argv[i], "--screenshot-frames") && i + 1 < argc) shotFrames = std::atoi(argv[++i]);
    else if ((!std::strcmp(argv[i], "--focus") || !std::strcmp(argv[i], "--tab")) && i + 1 < argc) focusName = argv[++i];
    else if (!std::strcmp(argv[i], "--select") && i + 1 < argc) selectName = argv[++i];
    else if (!std::strcmp(argv[i], "--ui-event") && i + 1 < argc) uiEvents.push_back(argv[++i]);
    else {
      std::fprintf(stderr, "usage: rensga-editor [--project <dir>] [--no-splash] [--splash-only] [--config-dir <dir>] [--play] [--quit-after N] [--no-auto-rebuild | --auto-rebuild] "
                           "[--screenshot <file.png> [--screenshot-frames N] [--select <object name>] [--focus <window> | --tab <window>] [--ui-event click|rclick|move:x,y ...]]\n");
      return 2;
    }
  }
  const bool shotMode = !shotFile.empty();

  App app;
  EditorLog& log = EditorLog::instance();
  log.info("rensga-editor started");
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
  Mode startMode = !projectDir.empty() ? Mode::Editor : (noSplash || (shotMode && !splashOnly)) ? Mode::Picker : Mode::Splash;
  int winW, winH;
  modeSize(startMode, shotMode, winW, winH);
  Uint32 wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
  if (startMode == Mode::Splash) wflags |= SDL_WINDOW_BORDERLESS; else wflags |= SDL_WINDOW_RESIZABLE;
  SDL_Window* window = SDL_CreateWindow("Rensga Engine", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, wflags);
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

  app.persistLayout = !shotMode;
  app.hub.mode = startMode;
  // Initialisation steps; the splash runs one per frame and shows what it is doing.
  static const char* const kSteps[] = {"Loading fonts and palette", "Registering console targets", "Preparing the renderer",
                                       "Reading settings and templates", "Ready"};
  auto runStep = [&](int i) {
    if (i == 1) {
      for (TargetModule* m : TargetRegistry::all()) log.info("target available: " + std::string(m->id()));
      if (!TargetRegistry::all().empty()) app.target = std::string(TargetRegistry::all()[0]->id());
    } else if (i == 2) {
      if (!app.renderer.init()) log.error("scene renderer failed to initialise");
    } else if (i == 3) {
      hubInit(app, configDir);
    }
  };
  if (startMode != Mode::Splash) for (int i = 0; i < 4; ++i) runStep(i);
  app.hub.initStep = startMode == Mode::Splash ? 0 : 4;
  app.scripts.enabled = autoRebuild == 1 || (autoRebuild == -1 && !shotMode);  // background script builds + hot reload
  if (!projectDir.empty()) {
    if (app.openProject(projectDir) && !shotMode) hubNoteOpened(app);
    if (shotMode) app.resetLayout = true;
    if (!selectName.empty()) {
      for (const SceneObject& o : app.doc.scene().objects)
        if (o.name == selectName) { app.doc.select(o.guid); break; }
      if (app.doc.selected().empty()) log.warn("--select: no object named " + selectName);
    }
  }

  Uint64 start = SDL_GetPerformanceCounter();
  const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
  bool running = true, playTriggered = false;
  size_t evIdx = 0;  // next --ui-event; one every 6 frames from frame 6, held back by the wait* verbs
  int evNext = 6;
  std::string pendingShot;
  std::string focusedPanel;
  int exitCode = 0, frame = 0;
  std::string lastTitle;
  bool skipSplash = false;
  while (running) {
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (app.hub.mode == Mode::Splash && (ev.type == SDL_MOUSEBUTTONDOWN || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)))
        skipSplash = true;
      ImGui_ImplSDL2_ProcessEvent(&ev);
      if (ev.type == SDL_QUIT) running = false;
      if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) scriptsOnFocus(app);
    }
    tickScripts(app);
    double t = static_cast<double>(SDL_GetPerformanceCounter() - start) / freq;
    if (autoPlay && !playTriggered && t > 0.3) {
      playTriggered = true;
      doPlay(app);
      // Idle after doPlay: it could not start, unless Play is waiting for the script rebuild (it resumes when the build ends)
      if (app.play.status() == PlayStatus::Idle && !app.scripts.builder.busy()) { exitCode = 1; running = false; }
    }
    if (autoPlay && playTriggered && !app.scripts.builder.busy() && app.scripts.failed) {
      log.error("play: script compile failed");
      exitCode = 1;
      running = false;
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
    // Splash: min 1.8 s, max 4 s, click/Esc skips; --splash-only (screenshots) stays until the shot.
    if (app.hub.mode == Mode::Splash) {
      const bool initDone = app.hub.initStep >= 4;
      if (!splashOnly && ((initDone && t >= 1.8 && !shotMode) || t >= 4.0 || (skipSplash && initDone))) {
        app.hub.mode = Mode::Picker;
        applyMode(window, Mode::Picker, shotMode);
        app.hub.status.clear();
      }
    }
    if (app.hub.goPicker && app.hub.mode == Mode::Editor) {
      saveLayout(app);
      app.saveCamera();
      app.play.stop();
      app.play.pump();
      app.loaded = false;
      app.hub.mode = Mode::Picker;
      app.hub.rescan = true;
      app.hub.error.clear();
      applyMode(window, Mode::Picker, shotMode);
    }
    app.hub.goPicker = false;
    if (!app.pendingProject.empty()) {
      if (app.loaded) { saveLayout(app); app.saveCamera(); }
      std::string dir = std::move(app.pendingProject);
      app.pendingProject.clear();
      if (app.openProject(dir)) {
        if (!shotMode) hubNoteOpened(app);
        app.hub.error.clear();
        if (app.hub.mode != Mode::Editor) { app.hub.mode = Mode::Editor; applyMode(window, Mode::Editor, shotMode); }
      } else {
        app.hub.error = "Cannot open " + dir + " (see the Console log).";
      }
    }
    if (!app.pendingLayout.empty()) {
      ImGui::LoadIniSettingsFromDisk(app.pendingLayout.c_str());
      app.pendingLayout.clear();
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    if (frame >= evNext && evIdx < uiEvents.size() && !uiEventHolds(app, uiEvents[evIdx])) {
      const std::string& e = uiEvents[evIdx++];
      evNext = frame + 6;
      float x = 0, y = 0;
      size_t c = e.find(':');
      const std::string act = e.substr(0, c), arg = c == std::string::npos ? std::string() : e.substr(c + 1);
      // test hooks that run the same code paths as the UI
      if (hubHook(app, act, arg)) {}
      else if (assetUiHook(app, act, arg)) {}                                  // F3.02: asset, assettab, assetset, vram, ...
      else if (act == "script") createScriptAction(app, arg);                       // Create > C++ Script
      else if (act == "component") createScriptAction(app, arg, ScriptKind::Component);  // Create > C++ Script > Empty Component
      else if (act == "openerror") openBuildError(arg.empty() ? 0 : std::atoi(arg.c_str()));  // double-click a console error
      else if (act == "cancelbuild") scriptsCancel(app);
      else if (act == "edit") scriptsTestEdit(app, arg);                       // edit:<file under project>|<old>|<new>
      else if (act == "shot") pendingShot = arg;                               // PNG of this frame, run goes on
      else if (act == "quit") running = false;
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
    if (app.hub.mode == Mode::Splash) {
      app.hub.status = kSteps[std::min(app.hub.initStep, 4)];
      app.hub.progress = std::min(1.0f, (app.hub.initStep / 4.0f) * 0.6f + 0.4f * std::min(1.0f, static_cast<float>(t) / 1.8f));
      drawSplashUi(app, static_cast<float>(t));
    } else if (app.hub.mode == Mode::Picker) {
      drawPicker(app);
    } else {
      drawMenuBar(app);
      drawToolbar(app);
      drawStatusBar(app);
      drawDockSpaceAndLayout(app);
      drawHierarchy(app);
      drawSceneView(app);
      drawGameView(app);
      drawInspector(app);
      drawProjectPanel(app);
      drawConsole(app);
      drawDialogs(app);
    }
    if (app.hub.mode != Mode::Splash) drawCreateDialogs(app);
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
    if (app.hub.mode == Mode::Splash) drawSplashBackground(shotMode ? frame * 0.05f : static_cast<float>(t), w, h);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    ++frame;
    // Proof that a script build never blocks the UI thread: tests read these lines (frame number + seconds).
    if (app.scripts.builder.busy() && frame % 20 == 0) std::fprintf(stderr, "rensga-editor: building frame=%d t=%.2f\n", frame, t);
    if (app.hub.mode == Mode::Splash && app.hub.initStep < 4 && frame >= 1) runStep(app.hub.initStep++);
    if (shotMode && frame >= shotFrames) {
      if (writeScreenshotPng(shotFile.c_str(), w, h)) std::printf("screenshot: %s (%dx%d, %d frames)\n", shotFile.c_str(), w, h, frame);
      else { std::fprintf(stderr, "screenshot failed: %s\n", shotFile.c_str()); exitCode = 1; }
      running = false;
    }
    if (!pendingShot.empty()) {
      if (writeScreenshotPng(pendingShot.c_str(), w, h)) std::printf("screenshot: %s (%dx%d, frame %d)\n", pendingShot.c_str(), w, h, frame);
      pendingShot.clear();
    }
    if (app.scripts.builder.busy() && frame % 60 == 0)  // proves the window keeps drawing while scripts compile
      std::printf("rensga-editor: frame %d (compiling scripts %d%%)\n", frame, app.scripts.builder.percent());
    SDL_GL_SwapWindow(window);

    if (io.WantSaveIniSettings) { saveLayout(app); io.WantSaveIniSettings = false; }
    std::string title = "Rensga Engine";
    if (app.hub.mode == Mode::Editor && app.loaded) title += " - " + app.project.name;
    if (app.doc.loaded()) title += " - " + app.doc.path().stem().string() + (app.doc.dirty() ? "*" : "");
    if (title != lastTitle) { SDL_SetWindowTitle(window, title.c_str()); lastTitle = title; }
  }

  saveLayout(app);
  app.saveCamera();
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
