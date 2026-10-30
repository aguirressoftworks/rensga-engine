#include "rensga/core/input.h"
#include "rensga_test.h"

using namespace rensga;

TEST(input_released_and_triggers_and_right_stick) {
  Input::clearActions();
  Input::defineButton("Jump", PackInputSource::South);
  VirtualGamepad pad;
  pad.held.south = true;
  pad.held.l2 = true;
  pad.rightX = 0.5f;
  pad.rightY = -0.5f;
  Input::setStickSettings(0.2f, 0.95f);
  Input::update(pad);
  const Vector2 expect = Input::ApplyRadialDeadZone(Vector2(0.5f, -0.5f), 0.2f, 0.95f);  // sticks come out after the dead zone
  CHECK(Input::GetButtonDown("Jump") && Input::GetButton("Jump") && !Input::GetButtonUp("Jump"));
  CHECK(Input::pad().pressed.l2 && !Input::pad().held.r2);
  CHECK_NEAR(Input::GetRightStick().x, expect.x, 1e-6f);
  CHECK_NEAR(Input::GetRightStick().y, expect.y, 1e-6f);
  CHECK(expect.x > 0.3f && expect.y < -0.3f);
  Input::update(pad);  // still held: no new press
  CHECK(!Input::GetButtonDown("Jump") && Input::GetButton("Jump"));
  pad.held.south = false;
  pad.held.l2 = false;
  Input::update(pad);
  CHECK(Input::GetButtonUp("Jump") && !Input::GetButton("Jump"));
  CHECK(Input::pad().released.l2);
  Input::update(pad);
  CHECK(!Input::GetButtonUp("Jump"));
  Input::clearActions();
}

namespace {
VirtualGamepad noPad() { return VirtualGamepad(); }
}

TEST(input_axis_ramps_like_unity_and_raw_is_instant) {
  Input::clearActions();
  InputActionDef d;
  d.name = "Steer";
  d.kind = PackInputKind::Axis;
  d.source = static_cast<u32>(PackInputSource::AxisRightX);
  d.bind[0] = kInputKeyBase + static_cast<u32>(KeyCode::E);
  d.bind[2] = kInputKeyBase + static_cast<u32>(KeyCode::Q);
  d.gravity = 3;
  d.sensitivity = 3;
  d.flags = kInputFlagSnap;
  Input::define(d);
  VirtualGamepad pad;
  pad.keys.set(KeyCode::E, true);
  Input::update(pad, 0.1f);
  CHECK_NEAR(Input::GetAxis("Steer"), 0.3f, 1e-5f);  // sensitivity 3 per second
  CHECK_NEAR(Input::GetAxisRaw("Steer"), 1.0f, 1e-6f);
  for (int i = 0; i < 10; ++i) Input::update(pad, 0.1f);
  CHECK_NEAR(Input::GetAxis("Steer"), 1.0f, 1e-6f);
  pad.keys.set(KeyCode::E, false);
  Input::update(pad, 0.1f);
  CHECK_NEAR(Input::GetAxis("Steer"), 0.7f, 1e-5f);  // gravity 3 per second
  CHECK_NEAR(Input::GetAxisRaw("Steer"), 0.0f, 1e-6f);
  pad.keys.set(KeyCode::Q, true);
  Input::update(pad, 0.1f);  // snap: reversing passes through 0 first
  CHECK_NEAR(Input::GetAxis("Steer"), -0.3f, 1e-5f);
  CHECK_NEAR(Input::GetAxisRaw("Steer"), -1.0f, 1e-6f);
  Input::clearActions();
}

TEST(input_key_edges_and_anykey) {
  Input::clearActions();
  VirtualGamepad pad;
  Input::update(pad);
  CHECK(!Input::anyKey() && !Input::GetKey(KeyCode::Space));
  pad.keys.set(KeyCode::Space, true);
  Input::update(pad);
  CHECK(Input::GetKey(KeyCode::Space) && Input::GetKeyDown(KeyCode::Space) && !Input::GetKeyUp(KeyCode::Space));
  CHECK(Input::anyKey() && Input::anyKeyDown());
  Input::update(pad);
  CHECK(Input::GetKey(KeyCode::Space) && !Input::GetKeyDown(KeyCode::Space) && !Input::anyKeyDown());
  pad.keys.set(KeyCode::Space, false);
  Input::update(pad);
  CHECK(!Input::GetKey(KeyCode::Space) && Input::GetKeyUp(KeyCode::Space) && !Input::anyKey());
}

TEST(input_actions_api_handles_and_edges) {
  Input::clearActions();
  Input::defineButton("Jump", PackInputSource::South);
  InputAction* jump = InputActions::Find("Jump");
  CHECK(jump != nullptr && InputActions::Find("Nope") == nullptr);
  VirtualGamepad pad;
  pad.held.south = true;
  Input::update(pad);
  CHECK(jump->WasPressedThisFrame() && jump->IsPressed() && !jump->WasReleasedThisFrame());
  CHECK(jump->ReadValue<bool>() && jump->ReadValue<Scalar>() == 1.0f);
  Input::update(pad);
  CHECK(!jump->WasPressedThisFrame() && jump->IsPressed());
  pad.held.south = false;
  Input::update(pad);
  CHECK(jump->WasReleasedThisFrame() && !jump->IsPressed());
  Input::clearActions();
}

TEST(input_composite_vector2_from_keys) {
  Input::clearActions();
  InputActionDef d;
  d.name = "Keys";
  d.kind = PackInputKind::Axis2;
  d.source = static_cast<u32>(PackInputSource::Composite);
  d.bind[0] = kInputKeyBase + static_cast<u32>(KeyCode::I);
  d.bind[2] = kInputKeyBase + static_cast<u32>(KeyCode::K);
  d.bind[4] = kInputKeyBase + static_cast<u32>(KeyCode::J);
  d.bind[6] = kInputKeyBase + static_cast<u32>(KeyCode::L);
  d.sensitivity = 1000;  // reach the target at once
  d.gravity = 1000;
  Input::define(d);
  VirtualGamepad pad;
  pad.keys.set(KeyCode::I, true);
  Input::update(pad, 0.1f);
  Vector2 v = Input::GetAxis2("Keys");
  CHECK_NEAR(v.x, 0.0f, 1e-5f);
  CHECK_NEAR(v.y, 1.0f, 1e-5f);
  pad.keys.set(KeyCode::L, true);
  Input::update(pad, 0.1f);
  v = Input::GetAxis2("Keys");
  CHECK_NEAR(v.x, 0.70710678f, 1e-4f);  // diagonal keys have unit length
  CHECK_NEAR(v.y, 0.70710678f, 1e-4f);
  CHECK(Input::Find("Keys")->IsPressed());
  Input::clearActions();
}

TEST(input_stick_dead_zone_math) {
  const Vector2 inside = Input::ApplyRadialDeadZone(Vector2(0.1f, 0.1f), 0.2f, 0.95f);
  CHECK(inside.x == 0 && inside.y == 0);
  const Vector2 edge = Input::ApplyRadialDeadZone(Vector2(0.2f, 0), 0.2f, 0.95f);
  CHECK_NEAR(edge.x, 0.0f, 1e-6f);
  const Vector2 mid = Input::ApplyRadialDeadZone(Vector2(0.575f, 0), 0.2f, 0.95f);
  CHECK_NEAR(mid.x, 0.5f, 1e-5f);
  const Vector2 full = Input::ApplyRadialDeadZone(Vector2(0.95f, 0), 0.2f, 0.95f);
  CHECK_NEAR(full.x, 1.0f, 1e-5f);
  const Vector2 diag = Input::ApplyRadialDeadZone(Vector2(1.0f, 1.0f), 0.2f, 0.95f);  // corner of the square gate clamps to 1
  CHECK_NEAR(diag.x, diag.y, 1e-6f);
  CHECK_NEAR(Mathf::Sqrt(diag.x * diag.x + diag.y * diag.y), 1.0f, 1e-5f);
  CHECK_NEAR(Input::stickFromByte(128), 0.0f, 1e-6f);
  CHECK_NEAR(Input::stickFromByte(255), 1.0f, 1e-6f);
  CHECK_NEAR(Input::stickFromByte(0), -1.0f, 1e-6f);
  // Through update(): a small stick value is gone, a large one is rescaled.
  Input::clearActions();
  Input::setStickSettings(0.2f, 0.95f);
  Input::defineAxis2("Aim", PackInputSource::RightStick);
  VirtualGamepad pad;
  pad.rightX = 0.15f;
  Input::update(pad);
  CHECK(Input::GetAxis2("Aim").x == 0);
  pad.rightX = 1.0f;
  Input::update(pad);
  CHECK_NEAR(Input::GetAxis2("Aim").x, 1.0f, 1e-5f);
  Input::clearActions();
}

namespace {
struct RumbleSpy : IPlatform {
  Scalar low = -1, high = -1, secs = -1;
  int calls = 0;
  bool init() override { return true; }
  void shutdown() override {}
  void pollInput(VirtualGamepad&) override {}
  double timeSeconds() override { return 0; }
  bool shouldQuit() override { return false; }
  void setVibration(Scalar l, Scalar h, Scalar s) override { low = l; high = h; secs = s; ++calls; }
};
}  // namespace

TEST(input_rumble_is_a_noop_without_the_capability) {
  RumbleSpy spy;
  Input::bindPlatform(&spy);
  Input::setRumbleSupported(false);
  CHECK(!Input::SetVibration(1, 1, 1));
  Input::flushVibration();
  CHECK(spy.calls == 0);
  Input::setRumbleSupported(true);
  CHECK(Input::SetVibration(0.5f, 2.0f, 0.2f));  // clamped to 0..1
  Input::flushVibration();
  CHECK(spy.calls == 1 && spy.low == 0.5f && spy.high == 1.0f);
  Input::update(noPad(), 0.1f);
  Input::flushVibration();
  CHECK(spy.calls == 1);  // still running
  Input::update(noPad(), 0.2f);  // effect over: a (0, 0) call goes out
  Input::flushVibration();
  CHECK(spy.calls == 2 && spy.low == 0 && spy.high == 0);
  Input::setRumbleSupported(false);
  Input::bindPlatform(nullptr);
}
