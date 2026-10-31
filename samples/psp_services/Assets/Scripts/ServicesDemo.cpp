#include <rensga/Rensga.h>
#include <stdio.h>
#include <string.h>

using namespace rensga;

// F5.13 / F5.15 / F5.16 Services scene: language, confirm button, battery, CPU clock, memory lines, an OSK name entry and a save
// through the system dialog. Every result is logged as "rensga: services <key>=<value>" (tools/ci/psp_services.sh greps them)
// and shown as a UIText line (objects S0..S7).
//   useOsk = true : the name comes from TouchScreenKeyboard (needs someone to drive the keyboard)
//   useOsk = false: the name is fixed ("Rensga"), for emulators with no input (PPSSPPHeadless: savedata runs as AUTOSAVE / AUTOLOAD)
// Confirm / cancel: a menu reads ConfirmSouth or ConfirmEast depending on Application::confirmButtonEast() (see Confirm()).
struct ServicesDemo : Behaviour {
  REFLECT(ServicesDemo)
  SERIALIZE_FIELD(bool, useOsk, true)

  u32 frame = 0;
  bool oskOpen = false, saved = false;
  char name[TouchScreenKeyboard::kMaxText] = {};
  InputAction* confirmSouth = nullptr;
  InputAction* confirmEast = nullptr;

  static const char* LanguageName(SystemLanguage l) {
    static const char* const n[] = {"English", "Japanese", "French", "Spanish", "German", "Italian", "Dutch", "Portuguese", "Russian",
                                    "Korean", "ChineseTraditional", "ChineseSimplified"};
    const u32 i = static_cast<u32>(l);
    return i < 12 ? n[i] : "Unknown";
  }

  static int Pct(Scalar level) { return level < 0 ? -1 : static_cast<int>(level * 100.0f + 0.5f); }

  void Line(int i, const char* text) {
    char obj[4] = {'S', static_cast<char>('0' + i), 0, 0};
    if (GameObject* o = GameObject::Find(obj))
      if (UIText* t = o->GetComponent<UIText>()) t->SetText(text);
  }

  // The button a menu treats as "confirm" right now: Cross on a normal PSP, Circle where the system says so.
  bool ConfirmPressed() const {
    InputAction* a = Application::confirmButtonEast() ? confirmEast : confirmSouth;
    return a && a->WasPressedThisFrame();
  }

  void OnApplicationPause(bool paused) override { Debug::Log("rensga: OnApplicationPause(%s)", paused ? "true" : "false"); }

  void Start() override {
    confirmSouth = InputActions::Find("ConfirmSouth");
    confirmEast = InputActions::Find("ConfirmEast");
    char b[UIText::kMaxText];
    const SystemLanguage lang = Application::systemLanguage();
    Debug::Log("rensga: services language=%d (%s)", static_cast<int>(lang), LanguageName(lang));
    const bool east = Application::confirmButtonEast();
    Debug::Log("rensga: services confirm=%s", east ? "east" : "south");
    const Scalar bat = SystemInfo::batteryLevel();
    Debug::Log("rensga: services battery=%d%% status=%d", Pct(bat), static_cast<int>(SystemInfo::batteryStatus()));
    Debug::Log("rensga: services memory system=%dMB used=%u graphics=%dMB graphics_used=%u", static_cast<int>(SystemInfo::systemMemorySize()),
               static_cast<unsigned>(SystemInfo::usedMemory()), static_cast<int>(SystemInfo::graphicsMemorySize()),
               static_cast<unsigned>(SystemInfo::usedGraphicsMemory()));
    Line(0, "SERVICES");
    snprintf(b, sizeof b, "LANGUAGE %s", LanguageName(lang));
    Line(1, b);
    Line(2, east ? "CONFIRM CIRCLE (EAST)" : "CONFIRM CROSS (SOUTH)");
    snprintf(b, sizeof b, "BATTERY %d PCT", Pct(bat));
    Line(3, b);
    snprintf(b, sizeof b, "MEM %d MB USED %u KB", static_cast<int>(SystemInfo::systemMemorySize()), static_cast<unsigned>(SystemInfo::usedMemory() / 1024));
    Line(4, b);
  }

  void Update() override {
    ++frame;
    if (frame == 10) {
      const bool a = Application::SetCpuClock(222);
      const bool b = Application::SetCpuClock(333);
      Debug::Log("rensga: services clock=333 ok=%d (222 first: %d)", b ? 1 : 0, a ? 1 : 0);
      Line(5, b ? "CLOCK 333 MHZ" : "CLOCK UNSUPPORTED");
    }
    if (frame == 60) {  // after the 30-frame autostart window of tools/ci/psp_services.sh
      if (useOsk) {
        oskOpen = TouchScreenKeyboard::Open("Ab", "Your name", 16);
        Debug::Log("rensga: services osk open=%d", oskOpen ? 1 : 0);
        Line(6, "TYPE A NAME");
      } else {
        memcpy(name, "Rensga", 7);
        Debug::Log("rensga: services name=%s", name);
        Save::setMode(SaveMode::SystemDialog);
        DoSave();
      }
    }
    if (oskOpen) {
      const TouchScreenKeyboard::Status s = TouchScreenKeyboard::status();
      if (s == TouchScreenKeyboard::Status::Done || s == TouchScreenKeyboard::Status::Canceled || s == TouchScreenKeyboard::Status::LostFocus) {
        oskOpen = false;
        if (s == TouchScreenKeyboard::Status::Done) {
          strncpy(name, TouchScreenKeyboard::text(), sizeof(name) - 1);
          Debug::Log("rensga: services name=%s", name);
          char b[96];
          snprintf(b, sizeof b, "NAME %s", name);
          Line(6, b);
          Save::setMode(SaveMode::SystemDialog);
          DoSave();
        } else {
          Debug::Log("rensga: services osk %s", s == TouchScreenKeyboard::Status::Canceled ? "cancelled" : "failed");
          Line(6, "NAME CANCELLED");
        }
      }
    }
    if (ConfirmPressed()) Debug::Log("rensga: services confirm pressed");
  }

  void DoSave() {
    if (saved) return;
    saved = true;
    const bool ok = Save::SaveFile("slot0", name, static_cast<u32>(strlen(name)) + 1);
    Debug::Log("rensga: services save ok=%d", ok ? 1 : 0);
    char back[TouchScreenKeyboard::kMaxText] = {};
    const i32 n = Save::LoadFile("slot0", back, sizeof(back) - 1);
    Debug::Log("rensga: services load bytes=%d name=%s", static_cast<int>(n), n > 0 ? back : "(none)");
    Line(7, ok && n > 0 ? "SAVED AND LOADED" : "SAVE FAILED");
    Debug::Log("rensga: services done");
  }
};
