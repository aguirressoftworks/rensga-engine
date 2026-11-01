// F6.04 Editor 10: UI editing in the Scene view (panels/canvas_edit.cpp).
#pragma once
#include <string>

#include "app.h"

namespace rensga {
namespace editor {

// GameObject > UI > (Canvas, Text, Image, Button, Slider, Toggle, Layout Group, EventSystem). `parent` is a GUID or "".
void drawUiCreateMenuItems(App& a, const std::string& parent, const float* at = nullptr);
// True while the pointer is over a UI element or handle drawn in the last Scene view frame (or a UI drag runs): the Scene view then leaves
// the click to the canvas editor instead of picking through the ID buffer.
bool canvasEditClaimsMouse();

}  // namespace editor
}  // namespace rensga
