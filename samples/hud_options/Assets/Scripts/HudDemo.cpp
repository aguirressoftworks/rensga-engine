#include <rensga/Rensga.h>
#include <stdio.h>

using namespace rensga;

// F6.04 / F6.12 samples/hud_options, scene Hud: a HUD built in the editor. A health bar is a UIImage with fillMethod Horizontal (the script only sets
// fillAmount), the score and the clock are UIText lines made with SetFormat (no heap, no printf of floats), and the saved option volumes are read
// back from PlayerPrefs and handed to the mixer groups when the scene starts, so a restart keeps the settings.
//   rensga: hud: volumes music=<n> sfx=<n> mixer music=<n> sfx=<n>      (percent; what was saved and what the mixer holds)
//   rensga: hud: tick=<n> score=<n> hp=<n>/<max> bar=<percent>             every 60 ticks
struct HudDemo : Behaviour {
  REFLECT(HudDemo)
  SERIALIZE_FIELD(i32, maxHealth, 100)
  TOOLTIP("Fixed steps between two hits / pickups.")
  RANGE(5, 200)
  SERIALIZE_FIELD(i32, period, 30)
  i32 hp = 0, score = 0, tick = 0;

  static UIText* TextOf(const char* name) {
    GameObject* go = GameObject::Find(name);
    return go ? go->GetComponent<UIText>() : nullptr;
  }
  static i32 Percent(Scalar v) { return static_cast<i32>(v * 100.0f + 0.5f); }

  void Start() override {
    hp = maxHealth;
    score = 0;
    tick = 0;
    const Scalar music = PlayerPrefs::GetFloat("vol_music", 0.5f), sfx = PlayerPrefs::GetFloat("vol_sfx", 0.5f);
    AudioMixerGroups::SetVolume(AudioGroup::Music, music);
    AudioMixerGroups::SetVolume(AudioGroup::SFX, sfx);
    Debug::Log("rensga: hud: volumes music=%d sfx=%d mixer music=%d sfx=%d", Percent(music), Percent(sfx),
               Percent(AudioMixerGroups::GetVolume(AudioGroup::Music)), Percent(AudioMixerGroups::GetVolume(AudioGroup::SFX)));
    Refresh();
  }

  void FixedUpdate() override {
    ++tick;
    if (tick % period == 0) {
      score += 125;
      hp -= 7;
      if (hp < 0) hp = maxHealth;  // the bar refills so the demo never ends
    }
    if (tick % 60 == 0) Debug::Log("rensga: hud: tick=%d score=%d hp=%d/%d bar=%d", tick, score, hp, maxHealth, Percent(static_cast<Scalar>(hp) / static_cast<Scalar>(maxHealth)));
    Refresh();
  }

  void Refresh() {
    if (GameObject* go = GameObject::Find("HealthFill"))
      if (UIImage* im = go->GetComponent<UIImage>()) im->fillAmount = static_cast<Scalar>(hp) / static_cast<Scalar>(maxHealth);
    if (UIText* t = TextOf("ScoreText")) t->SetFormat("SCORE {0:06}", score);
    if (UIText* t = TextOf("HpText")) t->SetFormat("HP {0}/{1}", hp, maxHealth);
    if (UIText* t = TextOf("TimerText")) t->SetFormat("{0:02}:{1:02}", tick / 3600, (tick / 60) % 60);
  }
};
