// F6 contract stubs (C0) for API 10 UI: Slider, Toggle, EventSystem. Safe defaults until D1 implements them (rensga/core/
// ui_components.h). Slider::SetValue and Toggle::SetIsOn store the value (so scripts and tests can read it back) but fire nothing.
#include "rensga/core/ui_components.h"

namespace rensga {

// TODO(D1 UI): clamp, wholeNumbers rounding, fire onValueChanged with the dynamic value, move the fill / handle objects.
void Slider::SetValue(Scalar v) {
  if (v < minValue) v = minValue;
  if (v > maxValue) v = maxValue;
  value = v;
}
// TODO(D1 UI): fire onValueChanged, show / hide `graphic`.
void Toggle::SetIsOn(bool on) { isOn = on; }

// TODO(D1 UI): first active EventSystem of the active world, the pad focus, explicit navigation.
EventSystem* EventSystem::current() { return nullptr; }
GameObject* EventSystem::currentSelectedGameObject() const { return nullptr; }
void EventSystem::SetSelectedGameObject(GameObject*) {}

}  // namespace rensga
