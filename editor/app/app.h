// Shared editor state and the panel entry points. No console names here: target data comes from TargetModule.
#pragma once
#include <imgui.h>

#include <filesystem>
#include <string>
#include <vector>

#include "rensga/editor/play_controller.h"
#include "rensga/editor/project.h"
#include "rensga/editor/scene_document.h"
#include "rensga/editor/target_module.h"
#include "theme.h"

namespace rensga {
namespace editor {

struct AssetNode {
  std::string name;
  bool dir = false;
  std::vector<AssetNode> children;
};

struct App {
  Project project;
  bool loaded = false;
  SceneDocument doc;
  PlayController play;
  std::string target;
  Fonts fonts;

  // Console filters
  bool showInfo = true, showWarn = true, showError = true, showEditor = true, showGame = true;
  bool scrollLog = true;

  // Panels (closable from Window menu)
  bool showHierarchy = true, showInspector = true, showConsole = true, showProject = true, showScene = true,
       showGame2 = true;

  // Layout
  bool resetLayout = false;       // request: rebuild the default Unity-style layout this frame
  bool focusScene = false;
  bool persistLayout = false;     // save <project>/Library/Editor/layout.ini
  std::filesystem::path layoutIni;

  // Dialogs / deferred actions
  bool openAbout = false, openProjectDialog = false;
  char openPath[512] = {};
  std::string pendingProject;     // handled between frames
  bool quitRequested = false;

  // Project panel cache
  AssetNode assets;
  double assetsStamp = -100;

  // Scene view placeholder state
  std::string status;             // last editor message for the status area

  bool openProject(const std::string& dir);  // loads project + first scene, sets up layout persistence
  bool saveScene();
  void refreshAssets();
  const TargetModule* targetModule() const;
};

void doPlay(App& a);

// panels (each draws its own ImGui window)
void drawMenuBar(App& a);
void drawToolbar(App& a);
void drawHierarchy(App& a);
void drawInspector(App& a);
void drawConsole(App& a);
void drawProjectPanel(App& a);
void drawSceneView(App& a);
void drawGameView(App& a);
void drawDialogs(App& a);

// layout
ImGuiID dockSpaceId();
void drawDockSpaceAndLayout(App& a);

// screenshot (stage tooling): reads the framebuffer and writes a PNG
bool writeScreenshotPng(const char* file, int w, int h);

}  // namespace editor
}  // namespace rensga
