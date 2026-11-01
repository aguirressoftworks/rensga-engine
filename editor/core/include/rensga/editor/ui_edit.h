// F6.04 Editor 10: UI authoring without ImGui. (1) Creating the GameObject > UI objects as one undo step each, (2) a headless
// preview of a scene's UI (a private engine World laid out for the target's resolution, so the Scene view shows exactly what the
// runtime draws: Canvas scaling, layout groups, slider fills, image fill), (3) rect math for the move / resize handles.
// Console-agnostic: the caller passes the view size.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "rensga/editor/scene_document.h"

namespace rensga {
namespace editor {

enum class UiKind { Canvas, Text, Image, Button, Slider, Toggle, LayoutGroup, EventSystem };
const char* uiKindName(UiKind k);                     // "Canvas", "Text", ...
bool uiKindFromName(const std::string& name, UiKind& out);  // case-insensitive; "layout" and "layoutgroup" both work

// Creates the object (with its helper children: a Button gets a Label, a Slider its Fill and Handle, a Toggle its Check and Label) under
// `parent` ("" = under the scene's Canvas, or the root for a Canvas / EventSystem). Elements other than Canvas need a Canvas: one is
// created first (ScaleWithScreenSize at viewW x viewH) when the scene has none, and interactive kinds also get an EventSystem. One undo
// step. Returns the new object's GUID, "" on failure.
std::string createUiObject(SceneDocument& doc, UiKind kind, const std::string& parent, int viewW, int viewH, const std::string& name = std::string());
// `name` replaces the default object name; helper children are named "<name>Label", "<name>Fill", "<name>Handle", "<name>Check" so scenes
// can address them (GameObject.Find) without ambiguity.

// ---- rect math shared with the runtime (UI::resolveRect) -----------------------------------------------------------------------
// x / y offsets that put the element's top-left corner at (left, top) for a w x h element with `anchor` in a vw x vh view.
void uiPlaceTopLeft(int anchor, int left, int top, int w, int h, int vw, int vh, int& x, int& y);
void uiResolve(int anchor, int x, int y, int w, int h, int vw, int vh, int out[4]);  // left, top, w, h

// ---- preview ----------------------------------------------------------------------------------------------------------------
struct UiElement {
  enum Kind { Image, Button, Text };
  std::string guid, name;
  Kind kind = Image;
  int component = -1;       // index of the component holding anchor / x / y (and w / h) in the object
  float x = 0, y = 0, w = 0, h = 0;      // real view pixels (the Canvas scale applied)
  float vx = 0, vy = 0, vw = 0, vh = 0;  // virtual pixels (what x / y / w / h of the fields mean)
  int anchor = 0;
  int fieldX = 0, fieldY = 0, fieldW = 0, fieldH = 0;
  bool hasSize = true;      // false for text (its size follows the text)
  bool driven = false;      // placed by a LayoutGroup or a Slider: the fields are overwritten at run time
  std::string drivenBy;
  unsigned char color[4] = {255, 255, 255, 255};
  std::string text;
  bool focusable = false, selectedByDefault = false;
  int sorting = 0;
  int fillMethod = 0;       // UIImage: 0 none, 1 horizontal, 2 vertical
  float fillAmount = 1;
  bool fillFromEnd = false;
  bool textured = false;
  bool hasSlider = false, hasToggle = false;
};

struct UiPreviewView {
  float scale = 1;
  int viewW = 0, viewH = 0, vw = 0, vh = 0;
  bool hasCanvas = false;
  int referenceW = 0, referenceH = 0, scaleMode = 0;
};

class UiPreview {
 public:
  UiPreview();
  ~UiPreview();
  // Rebuilds from the document (UI components only) and lays it out for a viewW x viewH view. False when the scene has no UI.
  bool rebuild(const SceneDocument& doc, int viewW, int viewH);
  const std::vector<UiElement>& elements() const { return elements_; }
  const UiPreviewView& view() const { return view_; }
  const UiElement* find(const std::string& guid) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::vector<UiElement> elements_;
  UiPreviewView view_;
};

// Does the scene have any UI component (UIText, UIImage, UIButton, Canvas, Slider, Toggle, LayoutGroup, EventSystem)?
bool sceneHasUi(const SceneDocument& doc);

}  // namespace editor
}  // namespace rensga
