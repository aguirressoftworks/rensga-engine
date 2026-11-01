// F6.04 / F6.12 (D1): the UI runtime on hand-built worlds (no pack): Format specs, Slider / Toggle events with a dynamic value, the
// EventSystem focus (automatic spatial, explicit, forced), the pad rules (submit follows the confirm button), layout groups and the
// Canvas scaler.
#include <string.h>

#include "rensga/Rensga.h"
#include "rensga/core/ui_components.h"
#include "rensga_test.h"

using namespace rensga;

namespace {

struct UiProbe : Behaviour {
  REFLECT(UiProbe)
  SERIALIZE_FIELD(i32, clicks, 0)
  METHOD(SetVolume)
  METHOD(SetOn)
  METHOD(SetLevelInt)
  METHOD(Click)
  Scalar volume = -1;
  i32 levelInt = -1;
  i32 on = -1;
  i32 uiClicks = 0;
  void SetVolume(Scalar v) { volume = v; }
  void SetOn(bool v) { on = v ? 1 : 0; }
  void SetLevelInt(i32 v) { levelInt = v; }
  void Click() { ++clicks; }
  void OnUIClick() override { ++uiClicks; }
};

struct UiFixture {
  Arena arena;
  World world;
  VirtualGamepad pad;
  UiFixture() {
    arena.create(1 << 20);
    WorldConfig c;
    c.maxObjects = 64;
    c.maxComponentsPerType = 32;
    world.init(c, arena);
    UI::resetFocus();
    Input::update(pad);
  }
  ~UiFixture() { UI::resetFocus(); world.shutdown(); }

  GameObject* button(const char* name, i32 x, i32 y, i32 w = 80, i32 h = 16, GameObject* parent = nullptr) {
    GameObject* go = world.createObject(name, parent);
    UIButton* b = go->AddComponent<UIButton>();
    b->x = x; b->y = y; b->w = w; b->h = h;
    return go;
  }
  // One frame: the buttons in `held` go down, UI::update runs, then they are released.
  void press(bool VirtualGamepad::Buttons::*b) {
    pad.held.*b = true;
    Input::update(pad);
    UI::update(world);
    pad.held.*b = false;
    Input::update(pad);
  }
  void frame() { Input::update(pad); UI::update(world); }
  const char* focus() { GameObject* g = UI::focused(); return g ? g->name : "(none)"; }
};

Guid mk(u8 n) {
  Guid g;
  g.bytes[0] = 0xD1;
  g.bytes[15] = n;
  return g;
}

}  // namespace

TEST(ui_format_specs_pad_and_round) {
  char b[48];
  Format(b, sizeof b, "{0:02}:{1:02}", 3, 7);
  CHECK(strcmp(b, "03:07") == 0);
  Format(b, sizeof b, "[{0:5}] [{1:05}] [{2:.1}] [{3:06.2}]", 42, -42, 2.46f, 3.14159f);
  CHECK(strcmp(b, "[   42] [-0042] [2.5] [003.14]") == 0);
  Format(b, sizeof b, "{0:4}|{1}", "ab", 12u);
  CHECK(strcmp(b, "  ab|12") == 0);
  Format(b, sizeof b, "Score {0}  x{1}  {2}", 1250, (u8)3, Fixed(0.5f, 1));
  CHECK(strcmp(b, "Score 1250  x3  0.5") == 0);
  Format(b, sizeof b, "{0:3}", 123456);
  CHECK(strcmp(b, "123456") == 0);          // the width is a minimum
  Format(b, sizeof b, "{9} {0:} {1:x}", 1, 2);
  CHECK(strncmp(b, " ", 1) == 0);           // a bad spec is printed as it is, a missing argument is empty
  UIText t;
  t.SetFormat("HP {0}/{1}", 7, 100);
  CHECK(strcmp(t.text, "HP 7/100") == 0);
}

TEST(ui_slider_clamps_rounds_and_fires_with_the_value) {
  UiFixture f;
  GameObject* go = f.world.createObject("Vol");
  Slider* s = go->AddComponent<Slider>();
  UiProbe* p = go->AddComponent<UiProbe>();
  go->guid = mk(1);
  UnityEventListener* l = s->onValueChanged.AddListener(go->guid, "UiProbe", "SetVolume");
  l->dynamic = 1;
  UnityEventListener* l2 = s->onValueChanged.AddListener(go->guid, "UiProbe", "SetLevelInt");
  l2->dynamic = 1;
  s->SetValue(0.5f);
  CHECK_NEAR(p->volume, 0.5f, 1e-5f);
  CHECK_EQ(p->levelInt, 0);                  // Scalar -> i32 truncates
  p->volume = -1;
  s->SetValue(0.5f);
  CHECK_NEAR(p->volume, -1.0f, 1e-5f);      // no change, no event
  s->SetValue(7.0f);
  CHECK_NEAR(s->GetValue(), 1.0f, 1e-5f);   // clamped to maxValue
  CHECK_NEAR(p->volume, 1.0f, 1e-5f);
  s->minValue = 0; s->maxValue = 10; s->wholeNumbers = true;
  s->SetValue(3.6f);
  CHECK_NEAR(s->GetValue(), 4.0f, 1e-5f);
  CHECK_EQ(p->levelInt, 4);
  // A listener that is not dynamic gets its stored argument.
  UnityEvent e;
  e.AddListener(go->guid, "UiProbe", "SetVolume")->setScalar(0.25f);
  MethodArg a;
  a.s = 9.0f;
  e.InvokeWith(f.world, MethodArgKind::Scalar, a);
  CHECK_NEAR(p->volume, 0.25f, 1e-5f);
}

TEST(ui_toggle_fires_bool_and_graphic_follows) {
  UiFixture f;
  GameObject* go = f.world.createObject("Mute");
  Toggle* t = go->AddComponent<Toggle>();
  UiProbe* p = go->AddComponent<UiProbe>();
  go->guid = mk(2);
  GameObject* mark = f.world.createObject("Check");
  mark->guid = mk(3);
  UIImage* im = mark->AddComponent<UIImage>();
  t->graphic = mark->guid;
  t->onValueChanged.AddListener(go->guid, "UiProbe", "SetOn")->dynamic = 1;
  UI::prepare(f.world);
  CHECK(!im->enabled());                    // off: the check mark is hidden
  t->SetIsOn(true);
  CHECK_EQ(p->on, 1);
  UI::prepare(f.world);
  CHECK(im->enabled());
  t->SetIsOn(true);
  t->SetIsOn(false);
  CHECK_EQ(p->on, 0);
}

TEST(ui_automatic_navigation_is_spatial_with_the_old_reading_order_as_fallback) {
  UiFixture f;
  // A 2 x 2 grid and one button below it.
  f.button("A", 10, 10); f.button("B", 120, 10); f.button("C", 10, 40); f.button("D", 120, 40); f.button("E", 60, 100);
  f.frame();
  CHECK(strcmp(f.focus(), "A") == 0);       // the first in reading order starts focused
  f.press(&VirtualGamepad::Buttons::right);
  CHECK(strcmp(f.focus(), "B") == 0);
  f.press(&VirtualGamepad::Buttons::down);
  CHECK(strcmp(f.focus(), "D") == 0);
  f.press(&VirtualGamepad::Buttons::left);
  CHECK(strcmp(f.focus(), "C") == 0);
  f.press(&VirtualGamepad::Buttons::up);
  CHECK(strcmp(f.focus(), "A") == 0);
  f.press(&VirtualGamepad::Buttons::down); f.press(&VirtualGamepad::Buttons::down);
  CHECK(strcmp(f.focus(), "E") == 0);
  f.press(&VirtualGamepad::Buttons::down);  // nothing below: wraps in reading order (the original rule)
  CHECK(strcmp(f.focus(), "A") == 0);
  f.press(&VirtualGamepad::Buttons::up);    // nothing above A: previous in reading order wraps to the last
  CHECK(strcmp(f.focus(), "E") == 0);
}

TEST(ui_explicit_navigation_none_and_forced_focus) {
  UiFixture f;
  GameObject* a = f.button("A", 10, 10);
  GameObject* b = f.button("B", 10, 40);
  GameObject* c = f.button("C", 10, 70);
  a->guid = mk(10); b->guid = mk(11); c->guid = mk(12);
  UIButton* ab = a->GetComponent<UIButton>();
  ab->navigation = 1;                        // Explicit
  ab->selectOnDown = c->guid;                // A down -> C, skipping B
  f.frame();
  f.press(&VirtualGamepad::Buttons::down);
  CHECK(strcmp(f.focus(), "C") == 0);
  f.press(&VirtualGamepad::Buttons::up);     // C is Automatic: the nearest above is B
  CHECK(strcmp(f.focus(), "B") == 0);
  f.press(&VirtualGamepad::Buttons::up);
  CHECK(strcmp(f.focus(), "A") == 0);
  f.press(&VirtualGamepad::Buttons::up);     // A has no selectOnUp: stays
  CHECK(strcmp(f.focus(), "A") == 0);
  ab->navigation = 2;                        // None: the pad never leaves it
  f.press(&VirtualGamepad::Buttons::down);
  CHECK(strcmp(f.focus(), "A") == 0);
  EventSystem::current();                    // no EventSystem in this world
  CHECK(EventSystem::current() == nullptr);
  GameObject* es = f.world.createObject("EventSystem");
  EventSystem* sys = es->AddComponent<EventSystem>();
  CHECK(EventSystem::current() == sys);
  sys->SetSelectedGameObject(b);
  CHECK(sys->currentSelectedGameObject() == b);
  f.frame();
  CHECK(strcmp(f.focus(), "B") == 0);        // a forced focus survives the next update
  Destroy(b);
  f.world.flushDestroyed();
  f.frame();
  CHECK(strcmp(f.focus(), "A") == 0);        // a destroyed focus falls back to the first selectable
}

TEST(ui_submit_follows_the_confirm_button_and_fires_onclick) {
  UiFixture f;
  GameObject* a = f.button("A", 10, 10);
  GameObject* b = f.button("B", 10, 40);
  a->guid = mk(20);
  UiProbe* p = a->AddComponent<UiProbe>();
  a->GetComponent<UIButton>()->onClick.AddListener(a->guid, "UiProbe", "Click");
  (void)b;
  f.world.startPending();                    // OnUIClick goes to started behaviours
  f.frame();
  f.press(&VirtualGamepad::Buttons::south);
  CHECK_EQ(p->clicks, 1);                    // onClick (UnityEvent)
  CHECK_EQ(p->uiClicks, 1);                  // OnUIClick (Behaviour)
  f.press(&VirtualGamepad::Buttons::east);
  CHECK_EQ(p->clicks, 1);                    // east is cancel on a platform that confirms with south
  UI::pushNav(UI::NavSubmit);                // a bot or a test: the same as a press
  f.frame();
  CHECK_EQ(p->clicks, 2);
  GameObject* na = f.world.createObject("N");
  (void)na;
  UI::pushNav(UI::NavDown);
  f.frame();
  CHECK(strcmp(f.focus(), "B") == 0);
}

TEST(ui_slider_takes_left_right_and_lets_up_down_through) {
  UiFixture f;
  GameObject* a = f.button("Top", 10, 10, 100, 16);
  (void)a;
  GameObject* so = f.world.createObject("Vol");
  UIImage* track = so->AddComponent<UIImage>();
  track->x = 10; track->y = 40; track->w = 100; track->h = 8;
  Slider* s = so->AddComponent<Slider>();
  s->value = 0.5f;
  GameObject* fo = f.world.createObject("Fill");
  fo->guid = mk(30);
  UIImage* fill = fo->AddComponent<UIImage>();
  s->fillRect = fo->guid;
  GameObject* ho = f.world.createObject("Handle");
  ho->guid = mk(31);
  UIImage* handle = ho->AddComponent<UIImage>();
  handle->w = 6; handle->h = 12;
  s->handleRect = ho->guid;
  f.frame();
  f.press(&VirtualGamepad::Buttons::down);
  CHECK(strcmp(f.focus(), "Vol") == 0);
  CHECK_EQ(fill->w, 50);                     // half of the 100 px track
  CHECK_EQ(fill->x, 10);
  f.press(&VirtualGamepad::Buttons::right);
  CHECK_NEAR(s->value, 0.6f, 1e-4f);         // a tenth of the range
  CHECK_EQ(fill->w, 60);
  CHECK_EQ(handle->x, 10 + 60 - 3);          // centred on the end of the fill
  f.press(&VirtualGamepad::Buttons::left); f.press(&VirtualGamepad::Buttons::left);
  CHECK_NEAR(s->value, 0.4f, 1e-4f);
  for (int i = 0; i < 12; ++i) f.press(&VirtualGamepad::Buttons::left);
  CHECK_NEAR(s->value, 0.0f, 1e-4f);         // clamped, no wrap
  CHECK(strcmp(f.focus(), "Vol") == 0);
  f.press(&VirtualGamepad::Buttons::up);
  CHECK(strcmp(f.focus(), "Top") == 0);      // up leaves a horizontal slider
}

TEST(ui_layout_group_places_children) {
  UiFixture f;
  GameObject* box = f.world.createObject("Box");
  UIImage* bi = box->AddComponent<UIImage>();
  bi->x = 20; bi->y = 30; bi->w = 200; bi->h = 40;
  LayoutGroup* lg = box->AddComponent<LayoutGroup>();
  lg->type = 0; lg->spacing = 10; lg->paddingLeft = 5; lg->paddingTop = 4;
  GameObject* c1 = f.world.createObject("c1", box);
  GameObject* c2 = f.world.createObject("c2", box);
  UIImage* i1 = c1->AddComponent<UIImage>(); i1->w = 30; i1->h = 10;
  UIImage* i2 = c2->AddComponent<UIImage>();
  i2->w = 50; i2->h = 20;
  UI::prepare(f.world);
  CHECK_EQ(i1->x, 25); CHECK_EQ(i1->y, 34);  // box left 20 + padding 5, box top 30 + padding 4 (UpperLeft)
  CHECK_EQ(i2->x, 25 + 30 + 10);
  lg->type = 1;                              // vertical
  UI::prepare(f.world);
  CHECK_EQ(i1->x, 25); CHECK_EQ(i1->y, 34);
  CHECK_EQ(i2->y, 34 + 10 + 10);
  lg->type = 2; lg->cellWidth = 40; lg->cellHeight = 20; lg->columns = 2; lg->spacing = 2;   // grid
  UI::prepare(f.world);
  CHECK_EQ(i2->x, 25 + 40 + 2); CHECK_EQ(i2->y, 34);
  lg->type = 0; lg->spacing = 10; lg->childAlignment = 4;      // MiddleCenter: the row is centred in the inner box
  UI::prepare(f.world);
  const i32 total = 30 + 10 + 50, inner = 200 - 5;
  CHECK_EQ(i1->x, 20 + 5 + (inner - total) / 2);
  CHECK_EQ(i1->y, 30 + 4 + ((40 - 4) - 10) / 2);
}

TEST(ui_canvas_scaler_maps_the_reference_resolution) {
  UiFixture f;
  UI::CanvasView none = UI::canvasView(f.world, 320, 240);
  CHECK_NEAR(none.scale, 1.0f, 1e-5f);       // no Canvas: nothing scales
  CHECK_EQ(none.vw, 320);
  GameObject* go = f.world.createObject("Canvas");
  Canvas* c = go->AddComponent<Canvas>();    // ScaleWithScreenSize is not the default: ConstantPixelSize
  UI::CanvasView k = UI::canvasView(f.world, 640, 448);
  CHECK_NEAR(k.scale, 1.0f, 1e-5f);
  c->scaleMode = 1; c->referenceWidth = 480; c->referenceHeight = 272; c->matchWidthOrHeight = 0.0f;   // match the width
  UI::CanvasView w = UI::canvasView(f.world, 640, 448);
  CHECK_NEAR(w.scale, 640.0f / 480.0f, 1e-4f);
  CHECK_EQ(w.vw, 480);
  c->matchWidthOrHeight = 1.0f;              // match the height
  UI::CanvasView h = UI::canvasView(f.world, 640, 448);
  CHECK_NEAR(h.scale, 448.0f / 272.0f, 1e-4f);
  CHECK_EQ(h.vh, 272);
  UI::CanvasView same = UI::canvasView(f.world, 480, 272);
  CHECK_NEAR(same.scale, 1.0f, 1e-5f);
  c->sortingOrder = 7;
  CHECK_EQ(UI::canvasView(f.world, 480, 272).order, 7);
}
