#include <rensga/Rensga.h>
#include <stdio.h>

using namespace rensga;

// F6.03 animator_player: a bot drives the Player animator controller (Idle, Run, Jump) through parameters, one step of the script per FIXED
// step, so the desktop player, the headless runner and the PSP simulator all see the same timeline.
//   speed  (float)   0 = standing, 1 = running: Idle <-> Run with a short cross-fade
//   jump   (trigger) any state -> Jump, which returns to Idle when its clip has played once; Idle goes back to Run while speed is 1
// It logs every state change and a summary line:
//   rensga: animator_player: state=Run step=61
//   rensga: animator_player: done steps=420 x=<px> changes=<n>
struct PlayerBot : Behaviour {
  REFLECT(PlayerBot)
  TOOLTIP("Fixed step at which the bot starts running.")
  SERIALIZE_FIELD(i32, runFrom, 60)
  TOOLTIP("Fixed step at which the bot stands still again.")
  SERIALIZE_FIELD(i32, runTo, 260)
  TOOLTIP("Fixed steps at which the jump trigger is set.")
  SERIALIZE_ARRAY(i32, jumpAt, 4)
  TOOLTIP("Fixed step at which the run ends (the script quits the game).")
  SERIALIZE_FIELD(i32, endAt, 420)

  i32 step = 0, changes = 0;
  u32 lastHash = 0;
  Scalar speed = 0;

  const char* StateName(const AnimatorStateInfo& i) const {
    if (i.IsName("Idle")) return "Idle";
    if (i.IsName("Run")) return "Run";
    if (i.IsName("Jump")) return "Jump";
    return "?";
  }

  void Start() override {
    jumpAt.Clear();
    jumpAt.Add(140);
    jumpAt.Add(330);
  }

  void FixedUpdate() override {
    ++step;
    Animator* a = gameObject()->GetComponent<Animator>();
    if (!a) return;
    speed = (step >= runFrom && step < runTo) ? 1.0f : 0.0f;
    a->SetFloat("speed", speed);
    for (u32 i = 0; i < jumpAt.count; ++i)
      if (step == jumpAt[i]) a->SetTrigger("jump");
    if (speed > 0) {
      Vector3 p = transform()->localPosition();
      p.x += 1.5f;
      transform()->setLocalPosition(p);
    }
    const AnimatorStateInfo info = a->GetCurrentStateInfo();
    if (info.nameHash != lastHash) {
      lastHash = info.nameHash;
      ++changes;
      Debug::Log("rensga: animator_player: state=%s step=%d", StateName(info), step);
    }
    if (step == endAt) {
      Debug::Log("rensga: animator_player: done steps=%d x=%d changes=%d", step, static_cast<int>(transform()->localPosition().x), changes);
      Application::Quit();
    }
  }

  void Update() override {
    Animator* a = gameObject()->GetComponent<Animator>();
    if (!a) return;
    GameObject* l1 = GameObject::Find("StateLabel");
    GameObject* l2 = GameObject::Find("ParamLabel");
    char buf[48];
    const AnimatorStateInfo info = a->GetCurrentStateInfo();
    if (l1) {
      snprintf(buf, sizeof buf, "state %s  t=%.2f", StateName(info), info.normalizedTime);
      if (UIText* t = l1->GetComponent<UIText>()) t->SetText(buf);
    }
    if (l2) {
      snprintf(buf, sizeof buf, "speed %.1f  step %d", a->GetFloat("speed"), step);
      if (UIText* t = l2->GetComponent<UIText>()) t->SetText(buf);
    }
  }
};
