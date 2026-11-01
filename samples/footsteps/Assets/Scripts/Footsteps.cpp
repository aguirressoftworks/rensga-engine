#include <rensga/Rensga.h>
#include <stdio.h>

using namespace rensga;

// F6.10 footsteps: the Run clip of the Player controller has two animation events (tick 0 and tick 2 of 4) that call Footstep(volume) on
// this script through the method registry; the script plays the step sound from the AudioSource on the same object (SFX group). An
// ambient hum (Ambience, Music group, playOnAwake) plays under it, and the script sets both mixer groups in Start.
//   rensga: footsteps: n=<k> step=<fixed step> vol=<v>      one line per event, a step every 10 fixed steps while running
//   rensga: footsteps: done steps=<n> footsteps=<k>
struct Footsteps : Behaviour {
  REFLECT(Footsteps)
  TOOLTIP("Fixed step at which the runner starts running.")
  SERIALIZE_FIELD(i32, idleUntil, 30)
  TOOLTIP("Fixed step at which the runner stops.")
  SERIALIZE_FIELD(i32, runUntil, 330)
  TOOLTIP("Fixed step at which the run ends (the script quits the game).")
  SERIALIZE_FIELD(i32, endAt, 380)
  METHOD(Footstep)

  i32 step = 0, count = 0;

  void Footstep(Scalar volume) {
    ++count;
    AudioSource* src = gameObject()->GetComponent<AudioSource>();
    if (src) src->PlayOneShot(src->clip, volume);
    const i32 pct = static_cast<i32>(volume * 100.0f + 0.5f);  // no printf of floats in game code: two decimals by hand
    Debug::Log("rensga: footsteps: n=%d step=%d vol=%d.%02d", count, step, pct / 100, pct % 100);
  }

  void Start() override {
    AudioMixerGroups::SetVolume(AudioGroup::Music, 0.4f);
    AudioMixerGroups::SetVolumeDb(AudioGroup::SFX, -3.0f);
  }

  void FixedUpdate() override {
    ++step;
    Animator* a = gameObject()->GetComponent<Animator>();
    if (!a) return;
    const bool run = step >= idleUntil && step < runUntil;
    a->SetFloat("speed", run ? 1.0f : 0.0f);
    if (run) {
      Vector3 p = transform()->localPosition();
      p.x += 1.2f;
      transform()->setLocalPosition(p);
    }
    if (step == endAt) {
      Debug::Log("rensga: footsteps: done steps=%d footsteps=%d", step, count);
      Application::Quit();
    }
  }

  void Update() override {
    GameObject* l = GameObject::Find("StateLabel");
    Animator* a = gameObject()->GetComponent<Animator>();
    if (!l || !a) return;
    char buf[48];
    snprintf(buf, sizeof buf, "%s  steps %d", a->GetCurrentStateInfo().IsName("Run") ? "Run" : "Idle", count);
    if (UIText* t = l->GetComponent<UIText>()) t->SetText(buf);
  }
};
