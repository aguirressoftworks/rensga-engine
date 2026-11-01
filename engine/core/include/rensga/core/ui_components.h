// F6 contract (C0): UI components for API 10. Fields are final; behaviour is stubbed until D1 (Stage 3). The existing UIText,
// UIImage and UIButton (gameplay.h) keep working unchanged; Canvas, Slider, Toggle, LayoutGroup and EventSystem are inert
// while D1 has not wired them. Methods are defined in engine/api/src/ui_api.cpp.
#pragma once
#include "rensga/core/object.h"
#include "rensga/core/unity_event.h"
#include "rensga/core/gameplay.h"

namespace rensga {

// Root of a UI tree. Elements under it lay out against the reference resolution; ScaleWithScreenSize scales them to the real
// view (the 9 anchors of UIAnchor still apply).
struct Canvas : Component {
  REFLECT(Canvas)
  SERIALIZE_ENUM(scaleMode, 0, "ConstantPixelSize|ScaleWithScreenSize")
  SERIALIZE_FIELD(i32, referenceWidth, 480)
  SERIALIZE_FIELD(i32, referenceHeight, 272)
  TOOLTIP("ScaleWithScreenSize: 0 matches the width, 1 matches the height.")
  RANGE(0, 1)
  SERIALIZE_FIELD(Scalar, matchWidthOrHeight, 0.5f)
  SERIALIZE_FIELD(i32, sortingOrder, 0)
};

namespace SliderDirection {
constexpr i32 LeftToRight = 0, RightToLeft = 1, BottomToTop = 2, TopToBottom = 3;
}

struct Slider : Component {
  REFLECT(Slider)
  SERIALIZE_FIELD(Scalar, minValue, 0.0f)
  SERIALIZE_FIELD(Scalar, maxValue, 1.0f)
  SERIALIZE_FIELD(Scalar, value, 0.0f)
  SERIALIZE_FIELD(bool, wholeNumbers, false)
  SERIALIZE_ENUM(direction, 0, "LeftToRight|RightToLeft|BottomToTop|TopToBottom")
  TOOLTIP("Object with a UIImage whose width (or height) follows the value.")
  SERIALIZE_FIELD(Guid, fillRect, Guid())
  TOOLTIP("Object that sits at the value on the bar.")
  SERIALIZE_FIELD(Guid, handleRect, Guid())
  SERIALIZE_FIELD(bool, interactable, true)
  TOOLTIP("Called when the value changes (by input or SetValue). The listener's own argument is passed as it is today; D1 adds the dynamic value.")
  SERIALIZE_FIELD(UnityEvent, onValueChanged, {})

  Scalar GetValue() const { return value; }
  void SetValue(Scalar v);  // clamps to [minValue, maxValue], rounds with wholeNumbers, fires onValueChanged when it changed
  Scalar normalizedValue() const { return maxValue > minValue ? (value - minValue) / (maxValue - minValue) : 0; }
};

struct Toggle : Component {
  REFLECT(Toggle)
  SERIALIZE_FIELD(bool, isOn, false)
  TOOLTIP("Object (UIImage) shown while the toggle is on, e.g. a check mark.")
  SERIALIZE_FIELD(Guid, graphic, Guid())
  SERIALIZE_FIELD(bool, interactable, true)
  SERIALIZE_FIELD(UnityEvent, onValueChanged, {})

  bool GetIsOn() const { return isOn; }
  void SetIsOn(bool on);  // fires onValueChanged when it changed
};

struct LayoutGroup : Component {
  REFLECT(LayoutGroup)
  SERIALIZE_ENUM(type, 0, "Horizontal|Vertical|Grid")
  SERIALIZE_FIELD(i32, spacing, 4)
  SERIALIZE_FIELD(i32, paddingLeft, 0)
  SERIALIZE_FIELD(i32, paddingRight, 0)
  SERIALIZE_FIELD(i32, paddingTop, 0)
  SERIALIZE_FIELD(i32, paddingBottom, 0)
  TOOLTIP("Grid only: size of a cell and the number of columns.")
  SERIALIZE_FIELD(i32, cellWidth, 32)
  SERIALIZE_FIELD(i32, cellHeight, 32)
  SERIALIZE_FIELD(i32, columns, 2)
  SERIALIZE_ENUM(childAlignment, 0, "UpperLeft|UpperCenter|UpperRight|MiddleLeft|MiddleCenter|MiddleRight|LowerLeft|LowerCenter|LowerRight")
};

// Settings of the scene's event system (UI focus and navigation). The first active one is used. D1 adds the pad navigation.
struct EventSystem : Component {
  REFLECT(EventSystem)
  TOOLTIP("Object (a UIButton, Slider or Toggle) that has the focus when the scene starts. Null = the first in reading order.")
  SERIALIZE_FIELD(Guid, firstSelected, Guid())
  TOOLTIP("Seconds a direction is held before it repeats, and seconds between repeats.")
  SERIALIZE_FIELD(Scalar, repeatDelay, 0.5f)
  SERIALIZE_FIELD(Scalar, repeatRate, 0.1f)

  // Unity's EventSystem.current (the first active one in the active world, null if none) and the focus. Defined in ui_api.cpp.
  static EventSystem* current();
  GameObject* currentSelectedGameObject() const;
  void SetSelectedGameObject(GameObject* go);
};

// ---- F6.04 / F6.12 (D1): the UI runtime (engine/core/src/ui_runtime.cpp) ------------------------------------------------------
class IRenderer;
class World;
namespace UI {

// How a Canvas maps its reference resolution to the real view. Without a Canvas (or ConstantPixelSize) scale is 1 and the virtual
// view is the real view. ScaleWithScreenSize: scale = sw + (sh - sw) * matchWidthOrHeight with sw = view / reference width and sh =
// view / reference height; the virtual view is the real view divided by the scale. Every UI element is laid out in virtual pixels.
struct CanvasView {
  Scalar scale = 1.0f;
  i32 viewW = 320, viewH = 240;  // real view
  i32 vw = 320, vh = 240;        // virtual view (anchors resolve against it)
  i32 order = 0;                 // Canvas.sortingOrder, added to every element's sorting order
};
// Real view size last given to renderGameplay2D (or the 320 x 240 default).
void viewSize(i32& w, i32& h);
CanvasView canvasView(World& world, i32 viewW, i32 viewH);
// Layout groups, slider fills and handles, toggle graphics. Idempotent; UI::update and the 2D drawing both call it.
void prepare(World& world);
// The same for a given real view size (the editor preview lays a scene out for the target's resolution without a renderer).
void prepare(World& world, i32 viewW, i32 viewH);
// Draws the UI (images with fill, buttons, sliders' parts, text) with the Canvas scale. Called by renderGameplay2D.
void draw(World& world, IRenderer& renderer);
// Rectangle of a UI object in virtual pixels (UIButton / UIImage / UIText, or the UIImage of a Slider / Toggle). False if it has none.
bool rectOf(GameObject* go, const CanvasView& view, Rect& out);

// Navigation input that does not come from the pad (bots, tests, on-screen controls): consumed by the next UI::update like a pad press.
enum Nav : u32 { NavUp = 1, NavDown = 2, NavLeft = 4, NavRight = 8, NavSubmit = 16, NavCancel = 32 };
void pushNav(u32 navBits);
// Called when the cancel button (the one the platform does not confirm with) is pressed; `focused` may be null.
using CancelHandler = void (*)(GameObject* focused);
void setCancelHandler(CancelHandler handler);
// The focused object (null if none) and a forced focus (what EventSystem::SetSelectedGameObject calls).
GameObject* focused();
void setFocused(GameObject* go);

}  // namespace UI

}  // namespace rensga
