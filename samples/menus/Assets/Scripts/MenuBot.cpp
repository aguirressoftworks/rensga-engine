#include <string.h>

#include <rensga/Rensga.h>

#include "MenuShared.h"

using namespace rensga;

// A scripted pad for the menus: it sends the same navigation as the pad (UI::pushNav) so an emulator or a headless run can walk the
// menus. It lives across scene loads. Title: Options, open it, volume down twice, up once, mute on and off, back to the buttons (Left lands on Quit), Up twice to Play, Play.
// Game: pause, move to Quit to title and back to Resume, resume, pause again, Quit to title. Back on the Title it quits.
// Logs "rensga: menus: bot finished" when done.
struct MenuBot : Behaviour {
  REFLECT(MenuBot)
  TOOLTIP("Frames between two bot presses.")
  RANGE(2, 120)
  SERIALIZE_FIELD(i32, gap, 12)

  enum Step : u8 { kNone, kUp, kDown, kLeft, kRight, kSubmit, kCancel, kPause };
  struct Plan { const char* scene; Step steps[24]; };
  i32 frame = 0, index = 0, titleVisits = 0;
  char scene[24] = {};

  static const Plan& PlanFor(const char* name, i32 visit) {
    static const Plan title = {"Title", {kDown, kSubmit, kLeft, kLeft, kRight, kDown, kSubmit, kSubmit, kLeft, kUp, kUp, kSubmit, kNone}};
    static const Plan game = {"Game", {kPause, kDown, kUp, kSubmit, kPause, kDown, kSubmit, kNone}};
    static const Plan done = {"Title", {kNone}};
    if (strcmp(name, "Game") == 0) return game;
    return visit <= 1 ? title : done;
  }

  void Start() override { DontDestroyOnLoad(gameObject()); }

  void Update() override {
    const char* now = SceneManager::activeSceneName();
    if (strcmp(now, scene) != 0) {
      strncpy(scene, now, sizeof scene - 1);
      frame = 0;
      index = 0;
      if (strncmp(scene, "Title", 5) == 0) ++titleVisits;
      Debug::Log("rensga: menus: bot in scene %s", scene);
    }
    ++frame;
    const Plan& plan = PlanFor(scene, titleVisits);
    if (frame < 40 || frame % gap != 0) return;
    const Step s = plan.steps[index];
    if (s == kNone) {
      if (strncmp(scene, "Title", 5) == 0 && titleVisits >= 2 && frame > 80) {
        Debug::Log("rensga: menus: bot finished");
        Application::Quit();
      }
      return;
    }
    ++index;
    switch (s) {
      case kUp: UI::pushNav(UI::NavUp); break;
      case kDown: UI::pushNav(UI::NavDown); break;
      case kLeft: UI::pushNav(UI::NavLeft); break;
      case kRight: UI::pushNav(UI::NavRight); break;
      case kSubmit: UI::pushNav(UI::NavSubmit); break;
      case kCancel: UI::pushNav(UI::NavCancel); break;
      case kPause: menus::botPause = true; break;
      default: break;
    }
  }
};
