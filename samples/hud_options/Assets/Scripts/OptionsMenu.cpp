#include <rensga/Rensga.h>
#include <stdio.h>

using namespace rensga;

// F6.04 / F6.12 samples/hud_options, scenes Options and OptionsVerify: two volume sliders (music, effects) built in the editor. Moving a slider
// reaches the mixer group at once and is stored in PlayerPrefs; Back saves and returns to the HUD. On start the saved values are read back and
// put on the sliders, which runs the same handlers, so a restart shows what was chosen.
//   rensga: options: loaded music=<n> sfx=<n> mixer music=<n> sfx=<n> (hasSaved=<0|1>)
//   rensga: options: music=<n> sfx=<n>                                     (a slider moved; percent)
//   rensga: options: saved music=<n> sfx=<n> ok=<0|1>                      (Back pressed)
struct OptionsMenu : Behaviour {
  REFLECT(OptionsMenu)
  TOOLTIP("0 = a person (or the bot) changes the sliders. 1 = verify run: only read back what was saved, log it and quit.")
  RANGE(0, 1)
  SERIALIZE_FIELD(i32, verifyOnly, 0)
  METHOD(SetMusic)
  METHOD(SetSfx)
  METHOD(Back)

  bool loading = true;
  i32 frame = 0;

  static i32 Percent(Scalar v) { return static_cast<i32>(v * 100.0f + 0.5f); }
  static Slider* SliderOf(const char* name) {
    GameObject* go = GameObject::Find(name);
    return go ? go->GetComponent<Slider>() : nullptr;
  }
  static void Label(const char* name, const char* what, Scalar v) {
    GameObject* go = GameObject::Find(name);
    UIText* t = go ? go->GetComponent<UIText>() : nullptr;
    if (t) t->SetFormat("{0} {1}%", what, Percent(v));
  }

  void Start() override {
    loading = true;
    const bool saved = PlayerPrefs::HasKey("vol_music");
    const Scalar music = PlayerPrefs::GetFloat("vol_music", 0.5f), sfx = PlayerPrefs::GetFloat("vol_sfx", 0.5f);
    if (Slider* s = SliderOf("MusicSlider")) s->SetValue(music);
    if (Slider* s = SliderOf("SfxSlider")) s->SetValue(sfx);
    AudioMixerGroups::SetVolume(AudioGroup::Music, music);
    AudioMixerGroups::SetVolume(AudioGroup::SFX, sfx);
    Label("MusicText", "MUSIC", music);
    Label("SfxText", "EFFECTS", sfx);
    Debug::Log("rensga: options: loaded music=%d sfx=%d mixer music=%d sfx=%d (hasSaved=%d)", Percent(music), Percent(sfx),
               Percent(AudioMixerGroups::GetVolume(AudioGroup::Music)), Percent(AudioMixerGroups::GetVolume(AudioGroup::SFX)), saved ? 1 : 0);
    loading = false;
  }

  void Update() override {
    ++frame;
    if (verifyOnly && frame == 30) {
      Debug::Log("rensga: options: verify done");
      Application::Quit();
    }
  }

  void SetMusic(Scalar v) {
    AudioMixerGroups::SetVolume(AudioGroup::Music, v);
    Label("MusicText", "MUSIC", v);
    if (loading) return;
    PlayerPrefs::SetFloat("vol_music", v);
    Debug::Log("rensga: options: music=%d sfx=%d", Percent(v), Percent(AudioMixerGroups::GetVolume(AudioGroup::SFX)));
  }
  void SetSfx(Scalar v) {
    AudioMixerGroups::SetVolume(AudioGroup::SFX, v);
    Label("SfxText", "EFFECTS", v);
    if (loading) return;
    PlayerPrefs::SetFloat("vol_sfx", v);
    Debug::Log("rensga: options: music=%d sfx=%d", Percent(AudioMixerGroups::GetVolume(AudioGroup::Music)), Percent(v));
  }
  void Back() {
    const bool ok = PlayerPrefs::Save();
    Debug::Log("rensga: options: saved music=%d sfx=%d ok=%d", Percent(PlayerPrefs::GetFloat("vol_music", 0.5f)), Percent(PlayerPrefs::GetFloat("vol_sfx", 0.5f)), ok ? 1 : 0);
    SceneManager::LoadScene("Hud");
  }
};
