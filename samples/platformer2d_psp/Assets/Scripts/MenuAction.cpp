#include <rensga/Rensga.h>

#include "GameState.h"

using namespace rensga;

// What a UIButton does when pressed (the button object also has this component). Menus are data: scenes pick the action.
struct MenuAction : Behaviour {
  REFLECT(MenuAction)
  TOOLTIP("0 New game, 1 Continue the saved game, 2 Resume (pause menu), 3 Quit to title, 4 Retry the room (game over).")
  RANGE(0, 4)
  SERIALIZE_FIELD(i32, action, 0)

  enum : i32 { kNewGame = 0, kContinue = 1, kResume = 2, kToTitle = 3, kRetry = 4 };

  static void Run(i32 action) {
    GameState::Sfx(GameState::kSfxSelect);
    switch (action) {
      case kNewGame: GameState::NewGame(); break;
      case kContinue: GameState::ContinueGame(); break;
      case kResume: GameState::paused = false; GameState::resumeFrame = Time::frameCount(); Time::setTimeScale(1.0f); break;
      case kToTitle: GameState::ToTitle(); break;
      case kRetry: GameState::RetryRoom(); break;
      default: break;
    }
  }
  void OnUIClick() override { Run(action); }
};
