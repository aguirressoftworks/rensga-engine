// F5.09 / F5.04: Window > Emulators (the emulator of the project's target: found or not, version against the pin, install, locate,
// reset its save area), the "emulator missing" offer when Play needs it, and Build and Run (release package, then the emulator).
// Logic is data from TargetModule::emulator() and the modules catalog (kind "emulator"); nothing here names a console.
#pragma once
#include <string>

namespace rensga {
namespace editor {

struct App;

enum class AfterEmulator { None, PlayConsole, BuildRun };

void openEmulatorManager(App& a);                 // Window > Emulators
void drawEmulatorManager(App& a);                 // every frame in Editor mode: the window, the offer dialog, the job tick
// Play on console and Build and Run call it: true = the emulator is there, go ahead. False = the offer dialog was opened (it
// continues with `after` once the emulator is installed or located) or the emulator cannot be installed here (the dialog says why).
bool gateTargetEmulator(App& a, const std::string& target, AfterEmulator after);
void doBuildAndRun(App& a);                       // release EBOOT of the target, then run it in its emulator
void stopBuildRun(App& a);

bool emulatorHook(App& a, const std::string& act, const std::string& arg);   // --ui-event emulators|emuinfo|emuinstall|emuaccept|emugate|emudialoginstall|emudialogcancel|emureset|buildrun|buildrunstop|buildrunstate
bool emulatorUiHolds(App& a, const std::string& event);                      // --ui-event buildrunwait:<running|finished>[|secs] holds the queue

}  // namespace editor
}  // namespace rensga
