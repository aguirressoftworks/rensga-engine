#include <rensga/Rensga.h>
#include <stdio.h>

#include "MenuShared.h"

using namespace rensga;

// F6.04 / F6.12 samples/menus: a title screen and a pause menu, both built in the editor (UI created with GameObject > UI, wired with
// UnityEvents) and fully navigable with the pad. The scene decides what this script does (mode): 0 = Title, 1 = Game with a pause menu.
//
//   Title: Play (loads Game), Options (shows OptionsPanel: a volume Slider and a mute Toggle that write to the Master mixer group), Quit.
//   Game : a running clock; Start (the "Menu" action) or Cancel opens the pause menu (Resume, Quit to title), time stops while it is open.
// Every focus move, press and value change prints a "rensga: ui ..." line (the navigation trace the CI checks).
struct MenuFlow : Behaviour {
  REFLECT(MenuFlow)
  TOOLTIP("0 Title scene, 1 Game scene.")
  RANGE(0, 1)
  SERIALIZE_FIELD(i32, mode, 0)
  METHOD(Play)
  METHOD(ToggleOptions)
  METHOD(Quit)
  METHOD(Resume)
  METHOD(ToTitle)
  METHOD(SetVolume)
  METHOD(SetMute)

  static inline MenuFlow* s_instance = nullptr;

  bool paused = false;
  i32 ticks = 0;

  static void SetActive(const char* name, bool on) {
    if (GameObject* go = GameObject::Find(name)) go->SetActive(on);
  }
  static void Focus(const char* name) {
    if (GameObject* go = GameObject::Find(name)) UI::setFocused(go);
  }
  static void OnCancel(GameObject*) {
    if (s_instance && s_instance->mode == 1 && s_instance->paused) s_instance->Resume();
  }

  void Start() override {
    s_instance = this;
    paused = false;
    ticks = 0;
    Time::setTimeScale(1.0f);
    UI::setCancelHandler(&OnCancel);
    if (mode == 0) {
      SetActive("OptionsPanel", false);
      UpdateVolumeLabel(AudioMixerGroups::GetVolume(AudioGroup::Master));
      Debug::Log("rensga: menus: title ready");
    } else {
      SetActive("PausePanel", false);
      Debug::Log("rensga: menus: game ready");
    }
  }
  void OnDestroy() override {
    if (s_instance == this) s_instance = nullptr;
    Time::setTimeScale(1.0f);
  }

  void Update() override {
    if (mode != 1) return;
    if (!paused) {
      ++ticks;
      GameObject* go = GameObject::Find("ClockText");
      UIText* t = go ? go->GetComponent<UIText>() : nullptr;
      if (t) t->SetFormat("TIME {0:02}:{1:02}", ticks / 3600, (ticks / 60) % 60);
      if (Input::GetButtonDown("Menu") || menus::botPause) {
        menus::botPause = false;
        Pause();
      }
    } else if (Input::GetButtonDown("Menu") || menus::botPause) {
      menus::botPause = false;
      Resume();
    }
  }

  void Pause() {
    paused = true;
    Time::setTimeScale(0.0f);
    SetActive("PausePanel", true);
    Focus("ResumeButton");
    Debug::Log("rensga: menus: paused at tick %d", ticks);
  }

  // ---- UnityEvent targets ----
  void Play() {
    Debug::Log("rensga: menus: play");
    SceneManager::LoadScene("Game");
  }
  void ToggleOptions() {
    GameObject* p = GameObject::Find("OptionsPanel");
    if (!p) return;
    p->SetActive(!p->activeSelf());
    Debug::Log("rensga: menus: options %s", p->activeSelf() ? "shown" : "hidden");
    if (p->activeSelf()) Focus("VolumeSlider");
  }
  void Quit() {
    Debug::Log("rensga: menus: quit");
    Application::Quit();
  }
  void Resume() {
    if (!paused) return;
    paused = false;
    Time::setTimeScale(1.0f);
    SetActive("PausePanel", false);
    Debug::Log("rensga: menus: resumed at tick %d", ticks);
  }
  void ToTitle() {
    Time::setTimeScale(1.0f);
    Debug::Log("rensga: menus: to title");
    SceneManager::LoadScene("Title");
  }
  void UpdateVolumeLabel(Scalar v) {
    GameObject* go = GameObject::Find("VolumeText");
    UIText* t = go ? go->GetComponent<UIText>() : nullptr;
    if (t) t->SetFormat("VOLUME {0}%", static_cast<i32>(v * 100.0f + 0.5f));
  }
  void SetVolume(Scalar v) {
    AudioMixerGroups::SetVolume(AudioGroup::Master, v);
    UpdateVolumeLabel(v);
  }
  void SetMute(bool on) {
    AudioMixerGroups::SetMuted(AudioGroup::Master, on);
    GameObject* go = GameObject::Find("MuteToggleLabel");
    UIText* t = go ? go->GetComponent<UIText>() : nullptr;
    if (t) t->SetText(on ? "MUTE ON" : "MUTE OFF");
  }
};
