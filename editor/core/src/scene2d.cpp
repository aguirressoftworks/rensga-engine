#include "rensga/editor/scene2d.h"

#include <fstream>

namespace rensga {
namespace editor {

namespace {
template <typename T>
T num(const nlohmann::json& f, const char* key, T def) {
  auto it = f.find(key);
  if (it == f.end() || !it->is_number()) return def;
  return it->get<T>();
}

void colorOf(const nlohmann::json& f, const char* key, float out[4]) {
  out[0] = out[1] = out[2] = out[3] = 1;
  auto it = f.find(key);
  if (it == f.end() || !it->is_array() || it->size() < 3) return;
  for (size_t i = 0; i < 4 && i < it->size(); ++i)
    if ((*it)[i].is_number()) out[i] = (*it)[i].get<float>() / 255.0f;
}

bool is2DType(const std::string& t) {
  return t == "SpriteRenderer" || t == "Tilemap" || t == "Camera2D" || t == "BoxCollider2D" || t == "UIImage" || t == "UIText";
}
}  // namespace

Scene2D gatherScene2D(const SceneDocument& doc) {
  Scene2D out;
  for (const SceneObject& o : doc.scene().objects) {
    if (!o.active) continue;
    double wd[16];
    doc.worldMatrix(o.guid, wd);
    for (const SceneComponent& c : o.components) {
      if (c.type == "SpriteRenderer") {
        Sprite2D s;
        s.guid = o.guid;
        s.model = m4FromDouble(wd);
        if (c.fields.contains("texture") && c.fields["texture"].is_string()) s.texture = c.fields["texture"].get<std::string>();
        s.srcX = num<int>(c.fields, "srcX", 0);
        s.srcY = num<int>(c.fields, "srcY", 0);
        s.srcW = num<int>(c.fields, "srcW", 0);
        s.srcH = num<int>(c.fields, "srcH", 0);
        s.pivotX = num<int>(c.fields, "pivotX", 0);
        s.pivotY = num<int>(c.fields, "pivotY", 0);
        s.sorting = num<int>(c.fields, "sortingOrder", 0);
        s.flipX = c.fields.value("flipX", false);
        colorOf(c.fields, "color", s.color);
        out.sprites.push_back(std::move(s));
      } else if (c.type == "Tilemap") {
        TileLayer2D t;
        t.guid = o.guid;
        t.model = m4FromDouble(wd);
        if (c.fields.contains("map") && c.fields["map"].is_string()) t.map = c.fields["map"].get<std::string>();
        t.sorting = num<int>(c.fields, "sortingOrder", -100);
        colorOf(c.fields, "tint", t.tint);
        out.layers.push_back(std::move(t));
      }
    }
  }
  return out;
}

bool sceneIs2D(const SceneDocument& doc) {
  bool any2d = false;
  for (const SceneObject& o : doc.scene().objects)
    for (const SceneComponent& c : o.components) {
      if (c.type == "MeshRenderer") return false;
      if (is2DType(c.type)) any2d = true;
    }
  return any2d;
}

bool readTilemap(const std::filesystem::path& file, TilemapData& out) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return false;
  nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
  if (j.is_discarded() || !j.is_object()) return false;
  out = TilemapData();
  out.tileset = j.value("tileset", "");
  out.tileSize = j.value("tileSize", 0);
  out.width = j.value("width", 0);
  out.height = j.value("height", 0);
  if (out.tileSize <= 0 || out.width <= 0 || out.height <= 0 || !j.contains("tiles") || !j["tiles"].is_array()) return false;
  for (const auto& v : j["tiles"]) out.tiles.push_back(v.is_number() ? v.get<int>() : 65535);
  out.tiles.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height), 65535);
  return true;
}

bool spriteLocalRect(const Sprite2D& s, const TextureSizeFn& size, float out[4]) {
  int w = s.srcW, h = s.srcH;
  if (w <= 0 || h <= 0) {
    int tw = 0, th = 0;
    if (s.texture.empty() || !size || !size(s.texture, tw, th)) return false;
    w = s.srcW > 0 ? s.srcW : tw;
    h = s.srcH > 0 ? s.srcH : th;
  }
  out[0] = static_cast<float>(-s.pivotX);
  out[1] = static_cast<float>(-s.pivotY);
  out[2] = out[0] + static_cast<float>(w);
  out[3] = out[1] + static_cast<float>(h);
  return true;
}

bool spriteWorldCorners(const Sprite2D& s, const TextureSizeFn& size, float out[12]) {
  float r[4];
  if (!spriteLocalRect(s, size, r)) return false;
  const float c[4][3] = {{r[0], r[1], 0}, {r[2], r[1], 0}, {r[2], r[3], 0}, {r[0], r[3], 0}};
  for (int i = 0; i < 4; ++i) m4Point(s.model, c[i], out + i * 3);
  return true;
}

}  // namespace editor
}  // namespace rensga
