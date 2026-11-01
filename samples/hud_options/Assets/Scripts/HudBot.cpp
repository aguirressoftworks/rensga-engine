#include <string.h>

#include <rensga/Rensga.h>

using namespace rensga;

// Scripted pad for hud_options. In the Hud scene (HudBot is Hud plus this bot) it waits, then goes to Options (Start). In Options it walks the pad like a player: music up 3,
// down to the effects slider, effects down 2, down to Back, confirm. Back saves the choice and loads the Hud, where the bot logs the clock for
// a while and quits. (Same navigation as the pad: UI::pushNav.)
struct HudBot : Behaviour {
  REFLECT(HudBot)
  TOOLTIP("Frames between two bot presses.")
  RANGE(2, 120)
  SERIALIZE_FIELD(i32, gap, 12)
  enum Step : u8 { kNone, kUp, kDown, kLeft, kRight, kSubmit, kOptions };
  i32 frame = 0, index = 0, hudVisits = 0;
  char scene[24] = {};

  void Start() override { DontDestroyOnLoad(gameObject()); }

  void Update() override {
    const char* now = SceneManager::activeSceneName();
    if (strcmp(now, scene) != 0) {
      strncpy(scene, now, sizeof scene - 1);
      frame = 0;
      index = 0;
      if (strncmp(scene, "Hud", 3) == 0) ++hudVisits;
      Debug::Log("rensga: hud-bot: scene %s", scene);
    }
    ++frame;
    static const Step options[] = {kRight, kRight, kRight, kDown, kLeft, kLeft, kDown, kSubmit, kNone};
    if (strncmp(scene, "Hud", 3) == 0) {
      if (hudVisits == 1 && frame == 150) {
        Debug::Log("rensga: hud-bot: opening options");
        SceneManager::LoadScene("Options");
      } else if (hudVisits >= 2 && frame == 150) {
        Debug::Log("rensga: hud-bot: finished");
        Application::Quit();
      }
      return;
    }
    if (strcmp(scene, "Options") != 0 || frame < 40 || frame % gap != 0) return;
    const Step s = options[index];
    if (s == kNone) return;
    ++index;
    switch (s) {
      case kUp: UI::pushNav(UI::NavUp); break;
      case kDown: UI::pushNav(UI::NavDown); break;
      case kLeft: UI::pushNav(UI::NavLeft); break;
      case kRight: UI::pushNav(UI::NavRight); break;
      case kSubmit: UI::pushNav(UI::NavSubmit); break;
      default: break;
    }
  }
};
