#include <rensga/Rensga.h>

using namespace rensga;

// F3.06 InputTest scene: every axis, button and key the Input API reports, plus rumble. Reach it with
// `rensga_desktop --pack game.rpak --scene InputTest`; the Title -> Room flow does not use it.
//   axis rows  : a label with the value and a bar (centre = 0 for -1..1 axes, left edge = 0 for triggers)
//   button rows: "[X] Name" in green while held, grey otherwise
//   rumble     : hold L (BumperL) and R (BumperR) together -> Input::SetVibration; the line shows the target's answer
//   Menu+Back  : back to the Title scene
// Uses cached InputAction* handles (Find once in Start), the Classic API (GetAxis, GetAxisRaw, GetKey, anyKey) side by side.
struct InputTestHud : Behaviour {
  REFLECT(InputTestHud)

  static constexpr int kAxisRows = 10;
  static constexpr int kButtons = 8;
  static constexpr int kBarX = 172, kBarW = 96;  // matches the IaB / IaF rows of InputTest.scene (fits 480x272)

  InputAction* horizontal = nullptr;
  InputAction* vertical = nullptr;
  InputAction* steer = nullptr;
  InputAction* aim = nullptr;
  InputAction* keys = nullptr;
  InputAction* trigL = nullptr;
  InputAction* trigR = nullptr;
  InputAction* buttons[kButtons] = {};
  Ref<UIText> axisLabel[kAxisRows];
  Ref<UIImage> axisFill[kAxisRows];
  Ref<UIText> buttonLabel[kButtons];
  Ref<UIText> keyLine, rumbleLine, hintLine, anyKeyLine;

  static const char* const* ButtonNames() {
    static const char* const n[kButtons] = {"Jump", "Fire", "Use", "Special", "BumperL", "BumperR", "Menu", "Back"};
    return n;
  }

  template <class T>
  static Ref<T> Get(const char* objectName) {
    GameObject* go = GameObject::Find(objectName);
    T* c = go ? go->GetComponent<T>() : nullptr;
    return c ? go->world->ref(c) : Ref<T>();
  }

  // "<name> +0.80" without printf (no heap, no float formatting on the consoles).
  static void Format(char* out, int cap, const char* name, Scalar v) {
    int n = 0;
    for (; name[n] && n < 12; ++n) out[n] = name[n];
    while (n < 13) out[n++] = ' ';
    int milli = static_cast<int>(v * 1000.0f + (v < 0 ? -0.5f : 0.5f));
    if (milli < 0) {
      out[n++] = '-';
      milli = -milli;
    } else {
      out[n++] = '+';
    }
    const int whole = milli / 1000, frac = (milli % 1000) / 10;
    if (n + 5 < cap) {
      out[n++] = static_cast<char>('0' + whole % 10);
      out[n++] = '.';
      out[n++] = static_cast<char>('0' + frac / 10);
      out[n++] = static_cast<char>('0' + frac % 10);
    }
    out[n] = 0;
  }

  void Start() override {
    horizontal = InputActions::Find("Horizontal");
    vertical = InputActions::Find("Vertical");
    steer = InputActions::Find("Steer");
    aim = InputActions::Find("Aim");
    keys = InputActions::Find("Keys");
    trigL = InputActions::Find("TriggerL");
    trigR = InputActions::Find("TriggerR");
    for (int i = 0; i < kButtons; ++i) buttons[i] = InputActions::Find(ButtonNames()[i]);
    char name[16];
    for (int i = 0; i < kAxisRows; ++i) {
      name[0] = 'I'; name[1] = 'a'; name[2] = 'L'; name[3] = static_cast<char>('0' + i); name[4] = 0;
      axisLabel[i] = Get<UIText>(name);
      name[2] = 'F';
      axisFill[i] = Get<UIImage>(name);
    }
    for (int i = 0; i < kButtons; ++i) {
      name[0] = 'I'; name[1] = 'b'; name[2] = static_cast<char>('0' + i); name[3] = 0;
      buttonLabel[i] = Get<UIText>(name);
    }
    keyLine = Get<UIText>("InKeys");
    anyKeyLine = Get<UIText>("InAnyKey");
    rumbleLine = Get<UIText>("InRumble");
    hintLine = Get<UIText>("InHint");
    if (UIText* t = Get<UIText>("InTitle").get()) t->SetText("INPUT TEST  AXES  BUTTONS  KEYS  RUMBLE");
    if (UIText* t = hintLine.get()) t->SetText("L + R = RUMBLE   MENU + BACK = TITLE");
  }

  void SetRow(int row, const char* name, Scalar v, bool bipolar) {
    char buf[UIText::kMaxText];
    Format(buf, sizeof buf, name, v);
    if (UIText* t = axisLabel[row].get()) t->SetText(buf);
    UIImage* f = axisFill[row].get();
    if (!f) return;
    const int half = kBarW / 2;
    const Scalar c = Mathf::Clamp(v, bipolar ? -1.0f : 0.0f, 1.0f);
    if (bipolar) {
      const int w = static_cast<int>(Mathf::Abs(c) * half);
      f->x = c >= 0 ? kBarX + half : kBarX + half - w;
      f->w = w;
    } else {
      f->x = kBarX;
      f->w = static_cast<int>(c * kBarW);
    }
  }

  void Update() override {
    if (!horizontal) return;
    const Vector2 a = aim ? aim->ReadValue<Vector2>() : Vector2();
    const Vector2 k = keys ? keys->ReadValue<Vector2>() : Vector2();
    SetRow(0, "Horizontal", horizontal->ReadValue<Scalar>(), true);
    SetRow(1, "Vertical", vertical->ReadValue<Scalar>(), true);
    SetRow(2, "Steer E/Q", steer->ReadValue<Scalar>(), true);        // ramps like Unity's GetAxis
    SetRow(3, "Steer raw", Input::GetAxisRaw("Steer"), true);         // -1, 0, 1 at once
    SetRow(4, "Aim X", a.x, true);
    SetRow(5, "Aim Y", a.y, true);
    SetRow(6, "Keys IJKL X", k.x, true);
    SetRow(7, "Keys IJKL Y", k.y, true);
    SetRow(8, "Trigger L2", trigL->ReadValue<Scalar>(), false);
    SetRow(9, "Trigger R2", trigR->ReadValue<Scalar>(), false);

    for (int i = 0; i < kButtons; ++i) {
      UIText* t = buttonLabel[i].get();
      if (!t || !buttons[i]) continue;
      const bool down = buttons[i]->IsPressed();
      char buf[24];
      int n = 0;
      buf[n++] = '[';
      buf[n++] = down ? 'X' : ' ';
      buf[n++] = ']';
      buf[n++] = ' ';
      for (const char* s = ButtonNames()[i]; *s && n < 22; ++s) buf[n++] = *s;
      buf[n] = 0;
      t->SetText(buf);
      t->color = down ? Color32(110, 230, 120, 255) : Color32(150, 150, 150, 255);
    }

    if (UIText* t = keyLine.get()) {
      char buf[UIText::kMaxText];
      int n = 0;
      auto add = [&](const char* s) { while (*s && n < 62) buf[n++] = *s++; };
      add("KEYS");
      if (Input::GetKey(KeyCode::Space)) add(" SPACE");
      if (Input::GetKey(KeyCode::E)) add(" E");
      if (Input::GetKey(KeyCode::Q)) add(" Q");
      if (Input::GetKey(KeyCode::LeftShift)) add(" SHIFT");
      if (Input::GetKeyDown(KeyCode::Return)) add(" RETURN-DOWN");
      buf[n] = 0;
      t->SetText(buf);
    }
    if (UIText* t = anyKeyLine.get()) t->SetText(Input::anyKey() ? "anyKey: yes" : "anyKey: no");

    // Combo: both bumpers held and one of them just went down -> rumble. The call is a no-op where the target has no rumble.
    if (buttons[4] && buttons[5] && buttons[4]->IsPressed() && buttons[5]->IsPressed() &&
        (buttons[4]->WasPressedThisFrame() || buttons[5]->WasPressedThisFrame())) {
      Input::SetVibration(0.6f, 1.0f, 0.4f);
    }
    if (UIText* t = rumbleLine.get()) {
      if (!Input::rumbleSupported()) {
        t->SetText("RUMBLE n/a on this target");
      } else {
        Scalar low, high, left;
        Input::currentVibration(low, high, left);
        char buf[UIText::kMaxText];
        Format(buf, sizeof buf, "RUMBLE lo", low);
        int n = 0;
        while (buf[n]) ++n;
        const char* mid = " hi ";
        for (int i = 0; mid[i] && n < 40; ++i) buf[n++] = mid[i];
        char hi[24];
        Format(hi, sizeof hi, "", high);
        for (int i = 13; hi[i] && n < 62; ++i) buf[n++] = hi[i];
        buf[n] = 0;
        t->SetText(buf);
      }
    }
    if (buttons[6] && buttons[7] && buttons[6]->IsPressed() && buttons[7]->IsPressed()) SceneManager::LoadScene("Title");
  }
};
