// F4.08 (Editor 7): sprite slices of a texture, the model behind the Sprite Editor. No ImGui, no GL, no console names.
//
// The slices live in the texture's `.meta`, under the top-level key "sprites" (beside "importSettings", which they never
// touch; the cooker ignores the key, a slice only fills in a SpriteRenderer's source rectangle and pivot):
//
//   "sprites": { "mode": "grid" | "manual",
//                "cellW": 16, "cellH": 16,          // grid only
//                "padding": 0 | [px, py],           // grid only: gap between cells (a number = both axes)
//                "offset": [ox, oy],                // grid only: top-left of the first cell, default [0, 0]
//                "rects": [ { "name": "player_0", "x": 0, "y": 0, "w": 16, "h": 16, "pivotX": 8, "pivotY": 16 }, ... ] }
//
// Rectangles are in texture pixels, origin top-left. The pivot is in pixels from the rectangle's top-left corner, the same
// meaning as SpriteRenderer::pivotX/pivotY. A grid keeps its rectangles in "rects" too, so a reader needs no grid maths and
// a cell can still be renamed or given its own pivot. Names are unique and not empty.
#pragma once
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace rensga {
struct SpriteRenderer;
namespace editor {

class AssetDatabase;

struct SpriteRect {
  std::string name;
  int x = 0, y = 0, w = 0, h = 0;
  int pivotX = 0, pivotY = 0;
  bool operator==(const SpriteRect& o) const {
    return name == o.name && x == o.x && y == o.y && w == o.w && h == o.h && pivotX == o.pivotX && pivotY == o.pivotY;
  }
};

enum class SliceMode { Grid, Manual };
enum class PivotPreset { TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight };

class SpriteSlices {
 public:
  SliceMode mode = SliceMode::Manual;
  int cellW = 0, cellH = 0;          // grid
  int paddingX = 0, paddingY = 0;    // grid: gap between cells
  int offsetX = 0, offsetY = 0;      // grid: top-left of the first cell
  std::vector<SpriteRect> rects;

  bool empty() const { return rects.empty(); }
  bool operator==(const SpriteSlices& o) const;

  // ---- the "sprites" JSON value
  nlohmann::json toJson() const;
  static bool fromJson(const nlohmann::json& j, SpriteSlices& out, std::string& err);

  // ---- grid
  // Sets mode to Grid and rebuilds `rects` from cellW/cellH/padding/offset for a texture of texW x texH: the full cells,
  // row by row. With keepExisting, the rectangle at the same index keeps its name and pivot. New cells are named
  // "<namePrefix>_<index>" with a top-left pivot. Returns the number of rectangles.
  int generateGrid(int texW, int texH, const std::string& namePrefix, bool keepExisting = true);
  static int gridColumns(int texW, int cellW, int paddingX, int offsetX);
  static int gridRows(int texH, int cellH, int paddingY, int offsetY);

  // ---- rectangles (also valid in a grid; editing one by hand does not change the grid fields)
  // Index of the rectangle called `name`, or -1.
  int find(const std::string& name) const;
  // The topmost (last) rectangle that contains the texel, or -1.
  int indexAt(int px, int py) const;
  // The rectangle with exactly this source rectangle, or -1.
  int indexOfSrc(int x, int y, int w, int h) const;
  // "<prefix>_<n>" with the lowest n that is not used.
  std::string uniqueName(const std::string& prefix) const;
  // Appends a rectangle; an empty or used name becomes uniqueName("sprite"). Returns its index, or -1 for w <= 0 or h <= 0.
  int addRect(const std::string& name, int x, int y, int w, int h);
  bool removeRect(int index);
  bool renameRect(int index, const std::string& name);       // false for an empty or used name
  bool setRect(int index, int x, int y, int w, int h);       // false for a bad index or w <= 0 or h <= 0
  bool setPivot(int index, int pivotX, int pivotY);
  bool setPivot(int index, PivotPreset preset);
  // Checks every rectangle against the texture size, names and sizes. `err` names the first problem.
  bool validate(int texW, int texH, std::string& err) const;
};

// The six SpriteRenderer fields a slice fills in.
struct SliceFields {
  int srcX = 0, srcY = 0, srcW = 0, srcH = 0;
  int pivotX = 0, pivotY = 0;
};
SliceFields sliceFields(const SpriteRect& r);
// Writes the slice into the renderer's srcX/srcY/srcW/srcH and pivotX/pivotY (the texture field is left alone).
void applySlice(SpriteRenderer& renderer, const SpriteRect& rect);
// Applies the slice called `name`; false (and the renderer untouched) when there is none.
bool applySlice(SpriteRenderer& renderer, const SpriteSlices& slices, const std::string& name);

// ---- the .meta of a texture asset
// A texture with no "sprites" key loads as empty slices (returns true). False only for a malformed value or unknown GUID.
bool loadSlices(const AssetDatabase& db, const std::string& textureGuid, SpriteSlices& out, std::string& err);
// Writes the slices; empty slices remove the key. Only "sprites" changes in the .meta.
bool saveSlices(AssetDatabase& db, const std::string& textureGuid, const SpriteSlices& slices, std::string& err);

}  // namespace editor
}  // namespace rensga
