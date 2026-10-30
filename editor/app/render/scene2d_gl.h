// F3.01: GL side of the Scene view's 2D layer. Decodes the textures and tilemaps that sprites and tile layers name by GUID
// (through a resolver, the asset database) and turns a Scene2D into draw-ordered quads for SceneRenderer.
#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "render/scene_renderer.h"
#include "rensga/editor/scene2d.h"

namespace rensga {
namespace editor {

class Scene2DResources {
 public:
  using Resolver = std::function<std::filesystem::path(const std::string& assetGuid)>;  // "" when unknown
  struct Tex { unsigned gl = 0; int w = 0, h = 0; long long stamp = 0; };

  void setResolver(Resolver r) { resolve_ = std::move(r); }
  // Main thread, current GL context. Files are re-read when their modification time changes (checked about once a second).
  const Tex* texture(const std::string& guid);
  bool textureSize(const std::string& guid, int& w, int& h);
  const TilemapData* tilemap(const std::string& guid);
  void clear();  // frees the GL textures

  // Quads for the sprites and tile layers, in draw order (sorting order, then file order). ids: object GUID -> pick id.
  std::vector<SpriteQuad> build(const Scene2D& scene, const std::map<std::string, unsigned>& ids);

 private:
  struct MapEntry { TilemapData data; bool ok = false; long long stamp = 0; };
  struct Checked { std::chrono::steady_clock::time_point at; };
  Resolver resolve_;
  std::map<std::string, Tex> tex_;
  std::map<std::string, MapEntry> maps_;
  std::map<std::string, std::chrono::steady_clock::time_point> checked_;
  bool due(const std::string& key);
};

}  // namespace editor
}  // namespace rensga
