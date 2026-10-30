// F3.06: InputActions.json -> INPT / INPX / INPS records.
#include <map>
#include <string>

#include "rensga/cooker/input_cook.h"
#include "rensga_test.h"

using namespace rensga;

TEST(input_cook_buttons_axes_composites_and_settings) {
  const char* text = R"({"format":1,"stickDeadZone":0.3,"stickOuterClamp":0.9,"actions":[
    {"name":"Jump","kind":"button","source":"south","alt":"key:Space"},
    {"name":"Move","kind":"axis2","source":"nub"},
    {"name":"Keys","kind":"axis2","source":"composite","up":"key:W","down":"key:S","left":"key:A","right":"key:D","gravity":4,"sensitivity":6},
    {"name":"Steer","kind":"axis","source":"rightx","positive":["key:E","r"],"negative":"key:Q","snap":true,"invert":true}]})";
  std::map<std::string, u32> strs;
  CookedInput out;
  std::vector<std::string> errors;
  cookInputActions(nlohmann::json::parse(text), [&](const std::string& s) {
    auto it = strs.find(s);
    if (it != strs.end()) return it->second;
    const u32 off = static_cast<u32>(strs.size() * 8);
    strs[s] = off;
    return off;
  }, out, errors);
  CHECK(errors.empty());
  CHECK(out.actions.size() == 4 && out.ext.size() == 4);
  CHECK_NEAR(out.settings.deadZone, 0.3f, 1e-6f);
  CHECK_NEAR(out.settings.outerClamp, 0.9f, 1e-6f);
  CHECK(out.actions[0].kind == static_cast<u32>(PackInputKind::Button));
  CHECK(out.actions[0].source == static_cast<u32>(PackInputSource::South));
  CHECK(out.ext[0].bind[0] == kInputKeyBase + static_cast<u32>(KeyCode::Space));
  CHECK(out.actions[1].kind == static_cast<u32>(PackInputKind::Axis2));
  CHECK(out.actions[2].source == static_cast<u32>(PackInputSource::Composite));
  CHECK(out.ext[2].bind[0] == kInputKeyBase + static_cast<u32>(KeyCode::W));
  CHECK(out.ext[2].bind[2] == kInputKeyBase + static_cast<u32>(KeyCode::S));
  CHECK(out.ext[2].bind[4] == kInputKeyBase + static_cast<u32>(KeyCode::A));
  CHECK(out.ext[2].bind[6] == kInputKeyBase + static_cast<u32>(KeyCode::D));
  CHECK_NEAR(out.ext[2].gravity, 4.0f, 1e-6f);
  CHECK_NEAR(out.ext[2].sensitivity, 6.0f, 1e-6f);
  CHECK(out.actions[3].kind == static_cast<u32>(PackInputKind::Axis));
  CHECK(out.ext[3].bind[0] == kInputKeyBase + static_cast<u32>(KeyCode::E));
  CHECK(out.ext[3].bind[1] == static_cast<u32>(PackInputSource::R));
  CHECK(out.ext[3].bind[2] == kInputKeyBase + static_cast<u32>(KeyCode::Q));
  CHECK((out.ext[3].flags & kInputFlagSnap) && (out.ext[3].flags & kInputFlagInvert));
}

TEST(input_cook_reports_unknown_bindings) {
  const char* text = R"({"format":1,"actions":[{"name":"Bad","kind":"button","source":"nope"}]})";
  CookedInput out;
  std::vector<std::string> errors;
  cookInputActions(nlohmann::json::parse(text), [](const std::string&) { return 0u; }, out, errors);
  CHECK(!errors.empty());
}
