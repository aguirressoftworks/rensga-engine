// F3.01: built-in gizmos drawn from the scene document, through the engine's Gizmos API (rensga/core/gizmos.h):
// BoxCollider2D rectangles, Camera2D view / dead zone / bounds, Tilemap bounds and tile grid, 3D collider shapes,
// the Camera frustum, the directional light ray, and the selection outline of sprites. The Scene view renders the list.
// Script OnDrawGizmos output lands in the same list. No ImGui, no console names.
#pragma once
#include <functional>
#include <string>

#include "rensga/core/math.h"
#include "rensga/editor/scene2d.h"
#include "rensga/editor/scene_document.h"

namespace rensga {
namespace editor {

struct DocumentGizmoStyle {
  Color collider{0.40f, 0.85f, 0.45f, 1};   // solid colliders
  Color trigger{0.35f, 0.75f, 0.95f, 1};    // trigger colliders
  Color camera{0.90f, 0.90f, 0.90f, 1};     // Camera2D view rectangle, camera frustum
  Color deadZone{0.95f, 0.60f, 0.25f, 1};   // Camera2D dead zone
  Color bounds{0.85f, 0.45f, 0.80f, 1};     // Camera2D bounds
  Color tilemap{0.55f, 0.65f, 0.95f, 1};    // Tilemap bounds and grid
  Color selection{1.00f, 0.80f, 0.00f, 1};  // selected objects
  Color light{1.00f, 0.95f, 0.55f, 1};
};

struct DocumentGizmoContext {
  DocumentGizmoStyle style;
  bool is2D = false;
  float viewW = 640, viewH = 448;  // the project's virtual resolution: the size of a Camera2D view
  TextureSizeFn textureSize;       // texture GUID -> pixels
  std::function<bool(const std::string& tilemapGuid, int& w, int& h, int& tileSize)> tilemapSize;  // tilemap asset GUID
};

// Appends to the Gizmos draw list (the caller clears it first with GizmoDraw::clear()).
void buildDocumentGizmos(const SceneDocument& doc, const DocumentGizmoContext& ctx);

}  // namespace editor
}  // namespace rensga
