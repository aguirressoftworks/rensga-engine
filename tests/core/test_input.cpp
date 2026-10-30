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
  pad.rightY = -1.0f;
  Input::update(pad);
  CHECK(Input::GetButtonDown("Jump") && Input::GetButton("Jump") && !Input::GetButtonUp("Jump"));
  CHECK(Input::pad().pressed.l2 && !Input::pad().held.r2);
  CHECK_NEAR(Input::GetRightStick().x, 0.5f, 1e-6f);
  CHECK_NEAR(Input::GetRightStick().y, -1.0f, 1e-6f);
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
