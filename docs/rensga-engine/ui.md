# UI (Editor 10, API 10, F6)

One UI system for every profile. UI objects ignore the Transform: each element keeps its own `anchor`, `x`, `y` (and `w`, `h`) in **virtual pixels**; the Hierarchy only organises them. The runtime is `engine/core/src/ui_runtime.cpp`; the editor side is `editor/app/panels/canvas_edit.cpp`, `input_actions.cpp`, `project_settings.cpp` and the console-agnostic `editor/core` documents (`ui_edit`, `input_actions_doc`, `physics2d_settings`).

## Components

| Component | What it does |
| --- | --- |
| `Canvas` | `scaleMode` ConstantPixelSize (1:1) or ScaleWithScreenSize. With the scaler, `referenceWidth x referenceHeight` maps to the view: `scale = sw + (sh - sw) * matchWidthOrHeight` with `sw = view width / reference width`, `sh = view height / reference height`. Elements are laid out in the virtual view (view / scale), then drawn scaled. Text glyphs are not scaled. `sortingOrder` is added to every element. The first active Canvas decides. A scene without a Canvas behaves as before F6. |
| `UIText` | `label` (31 characters) is the text typed in the editor. It is copied into `text` once, unless a script already set `text`. `SetText`, `SetInt` and `SetFormat("HP {0}/{1}", hp, max)` (no heap) are the script side. |
| `UIImage` | `fillMethod` None / Horizontal / Vertical, `fillAmount` 0..1, `fillFromEnd`. Horizontal fills from the left, Vertical from the bottom; the picture is cropped, not squeezed. |
| `UIButton` | Focusable. `navigation` Automatic / Explicit (`selectOnUp/Down/Left/Right`; empty = stay) / None. `onClick` is a UnityEvent; `Behaviour::OnUIClick` is still called. |
| `Slider` | `minValue`, `maxValue`, `value`, `wholeNumbers`, `direction`. The slider object has a `UIImage` (the track); `fillRect` and `handleRect` name objects whose `UIImage` the runtime resizes and moves. Left/right (up/down for vertical sliders) change the value by a tenth of the range (1 with `wholeNumbers`); `onValueChanged` fires with the new value. |
| `Toggle` | `isOn`, `graphic` (an object shown only while on). A press toggles it; `onValueChanged` fires with the new state. |
| `LayoutGroup` | Horizontal / Vertical / Grid, `spacing`, padding, cell size, `childAlignment`. The group object has a `UIImage`/`UIButton` that is its box (else the whole view). Its direct child objects are placed by the group; their own `x`/`y` are overwritten at run time. |
| `EventSystem` | Optional. `firstSelected` focuses a given object first; `repeatDelay` / `repeatRate` repeat a held direction. Without one, the old rules apply (first button focused, no repeat). |

## Focus and navigation

- Selectables are active, interactable `UIButton`, `Slider`, `Toggle` (the last two use the `UIImage` of their object as their rectangle). `EventSystem::current()->currentSelectedGameObject()` reads the focus; `SetSelectedGameObject(go)` (or `UI::setFocused`) sets it.
- Automatic navigation: the nearest selectable in that direction (distance along the direction plus twice the off-axis distance). With none that way it falls back to the original rule: Down/Right = next in reading order (top to bottom, then left to right), Up/Left = previous, both wrapping. A menu column therefore behaves as it always did.
- Submit is the platform confirm button (`Application::confirmButtonEast()`: south, or east on a PSP set to circle-confirms); the other face button is cancel (`UI::setCancelHandler`).
- `UI::pushNav(UI::NavDown | ...)` queues presses that are consumed like pad presses (bots, tests, on-screen controls).
- A pointer (mouse, finger) selects and presses what is under it; a slider follows a held pointer.
- Log lines (the navigation trace): `rensga: ui focus <name>`, `rensga: ui click <name>`, `rensga: ui slider <name> value_x100=<n>`, `rensga: ui toggle <name> isOn=<0|1>`.

## UnityEvent with a value

A listener marked `dynamic` (`"dynamic": true` in the scene JSON, the **Dynamic (event value)** checkbox in the inspector) receives the value the event is raised with instead of a stored argument: `Slider.onValueChanged` -> `void SetVolume(Scalar)`. The value is converted to the method's argument kind (Scalar <-> i32 truncates, bool is "not zero"). `UnityEvent::InvokeWith(world, kind, arg)` raises such an event from code. The flag lives in the reserved byte of `UnityEventListener` (still 44 bytes); old packs have 0 there.

## Editor

- **GameObject > UI** creates Canvas, Text, Image, Button, Slider, Toggle, Layout Group, EventSystem as one undo step; a missing Canvas (ScaleWithScreenSize at the target's resolution) and EventSystem are created first.
- **Scene view, 2D mode**: the canvas frame at the target's resolution, every element as the runtime draws it (a private engine World is laid out for the target, so layout groups, slider fills and image fill show exactly), a selection outline with eight rect handles, drag to move, drag a handle to resize (one undo step per drag). Elements driven by a layout group or a slider are marked and have no handles.
- **Window > Input Actions** edits `ProjectSettings/InputActions.json`: actions, kinds (button, axis, 2D axis), the virtual-gamepad source, keyboard bindings, tuning; validation is the cooker's own. On a phone the on-screen controls send the same pad buttons, so pad bindings are touch bindings.
- **Window > Project Settings**: Tags and Layers (`TagManager.json`), Physics (`Physics.json`: gravity, the 3D collision matrix), Physics 2D (`Physics2D.json`), Time (`Time.json`), Scenes in Build (`Project.json`), Video (read only, from the ConsoleTarget). A tab's file is written only when that tab changed.
