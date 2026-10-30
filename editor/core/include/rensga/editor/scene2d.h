// F3.01: the 2D layer of a scene document, as the Scene view draws and picks it: SpriteRenderer quads and Tilemap layers.
// 2D pixel space: world units are pixels, +Y points down, a sprite's top-left is the Transform position minus its pivot.
// No ImGui, no GL, no console names. Textures and tilemaps are resolved by GUID by the caller (the asset database).
#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "rensga/editor/scene_document.h"
#include "rensga/editor/viewport.h"

namespace rensga {
namespace editor {

struct Sprite2D {
  std::string guid;      // object
  std::string texture;   // texture asset GUID ("" = none: drawn as a flat rectangle)
  M4 model = {};         // object world matrix
  int srcX = 0, srcY = 0, srcW = 0, srcH = 0;  // source rect in texture pixels; srcW 0 = whole texture
  int pivotX = 0, pivotY = 0;
  int sorting = 0;
  bool flipX = false;
  float color[4] = {1, 1, 1, 1};
};

struct TileLayer2D {
  std::string guid;      // object
  std::string map;       // .tilemap.json asset GUID
  M4 model = {};
  float tint[4] = {1, 1, 1, 1};
  int sorting = -100;
};

struct Scene2D {
  std::vector<Sprite2D> sprites;
  std::vector<TileLayer2D> layers;
  bool any() const { return !sprites.empty() || !layers.empty(); }
};

// Active objects only, in file order.
Scene2D gatherScene2D(const SceneDocument& doc);
// True when the scene has 2D components (SpriteRenderer, Tilemap, Camera2D, BoxCollider2D, UI) and no 3D MeshRenderer:
// the Scene view opens it in 2D mode.
bool sceneIs2D(const SceneDocument& doc);

struct TilemapData {
  std::string tileset;  // texture asset GUID
  int tileSize = 0, width = 0, height = 0;
  std::vector<int> tiles;  // width * height, row-major; 65535 = empty
};
bool readTilemap(const std::filesystem::path& file, TilemapData& out);

using TextureSizeFn = std::function<bool(const std::string& textureGuid, int& w, int& h)>;
// Local rectangle of a sprite (x0, y0, x1, y1), pivot applied; false when its size is unknown.
bool spriteLocalRect(const Sprite2D& s, const TextureSizeFn& size, float out[4]);
// Local corners of a sprite (4 x xyz, clockwise from the top-left) transformed to world space.
bool spriteWorldCorners(const Sprite2D& s, const TextureSizeFn& size, float out[12]);

}  // namespace editor
}  // namespace rensga
