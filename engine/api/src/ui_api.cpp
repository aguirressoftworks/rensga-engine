// F6.12 (D1): Slider::SetValue, Toggle::SetIsOn, EventSystem::current / currentSelectedGameObject / SetSelectedGameObject and
// UIButton::isFocused are implemented with the rest of the UI runtime in engine/core/src/ui_runtime.cpp (they need the focus table
// and the World). This file stays so the API library keeps the translation unit the F6 contract listed.
#include "rensga/core/ui_components.h"
