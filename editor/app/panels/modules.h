// Build support modules in the editor (Unity Hub "Add modules"): the Hub > Modules tab, Window > Build Support Modules,
// the "module is missing" dialog for Play on console, and the --ui-event verbs that drive them.
// Logic lives in editor/core modules_model.h; this file is drawing and wiring only.
#pragma once
#include <memory>
#include <string>

#include "rensga/editor/modules_model.h"

namespace rensga {
namespace editor {

struct App;

enum class AfterModule { None, PlayConsole };

struct ModulesUi {
  std::unique_ptr<ModulesModel> model;
  bool showWindow = false;          // Window > Build Support Modules
  bool hubTab = false;              // Hub: Modules tab selected
  std::string selected;             // module shown in the details pane
  std::string notice;               // last result sentence (install, remove, verify)
  int noticeKind = 0;               // 0 info, 1 ok, 2 warn, 3 error
  bool noPrompt = false;            // --play: never open a dialog, let the pipeline report the missing module
  // missing-module dialog
  bool openGate = false;
  std::string gateModule;           // module the dialog installs
  std::string gateTarget;
  AfterModule gateAfter = AfterModule::None;
  bool gateStarted = false;         // the install was started from the dialog
  // Locate... popup (manual modules)
  bool openLocate = false;
  std::string locateModule;
  char locatePath[512] = {};
  std::string locateError;
  bool settingsLoaded = false;
};

ModulesUi& modulesUi();
ModulesModel& modulesModel(App& a);   // created on first use; picks up the located folders saved in the editor settings

// Hub: tab strip under the header. True when the Modules tab is active and was drawn (the caller skips the project list).
bool drawHubTabs(App& a, float footerHeight);
void drawModulesWindow(App& a);       // Window > Build Support Modules (floating)
void drawModuleDialogs(App& a);       // missing-module dialog + Locate... popup, every frame outside the splash
void tickModules(App& a);             // once per frame: join a finished install job, update the notice
// Play on console (and any build that needs a target's toolchain): true = go ahead. False = the dialog was opened
// (it continues with `after` once the module is installed) or the module cannot be installed here.
bool gateTargetModule(App& a, const std::string& target, AfterModule after);
// One line for the Create Project dialog: "<Name> is not installed (165 MB)", empty when fine.
std::string missingModuleLine(App& a, const std::string& target);
void openModulesFromHub(App& a);

bool modulesHook(App& a, const std::string& act, const std::string& arg);   // --ui-event modules|modselect|modinstall|modaccept|modremove|modverify|modlocate|modstate|playconsole|modgate|moddialoginstall|moddialogcancel
bool modulesUiHolds(App& a, const std::string& event);                       // --ui-event modwait holds the queue while a job runs

}  // namespace editor
}  // namespace rensga
