#define GL_GLEXT_PROTOTYPES 1
#include "render/scene2d_gl.h"

#include <SDL_opengl.h>

#include <algorithm>
#include <fstream>
#include <iterator>

#include "rensga/cooker/assets.h"

namespace rensga {
namespace editor {
namespace fs = std::filesystem;

namespace {
long long stampOf(const fs::path& p) {
  std::error_code ec;
  auto t = fs::last_write_time(p, ec);
  return ec ? 0 : static_cast<long long>(t.time_since_epoch().count());
}
}  // namespace

bool Scene2DResources::due(const std::string& key) {
  const auto now = std::chrono::steady_clock::now();
  auto it = checked_.find(key);
  if (it != checked_.end() && now - it->second < std::chrono::seconds(1)) return false;
  checked_[key] = now;
  return true;
}

const Scene2DResources::Tex* Scene2DResources::texture(const std::string& guid) {
  if (guid.empty() || !resolve_) return nullptr;
  auto it = tex_.find(guid);
  if (it != tex_.end() && !due("t:" + guid)) return it->second.gl ? &it->second : nullptr;
  const fs::path path = resolve_(guid);
  if (path.empty()) return nullptr;
  const long long stamp = stampOf(path);
  if (it != tex_.end() && it->second.stamp == stamp) return it->second.gl ? &it->second : nullptr;
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  ImageRgba img;
  std::string err;
  Tex t;
  t.stamp = stamp;
  if (!bytes.empty() && decodePng(bytes, img, err) && img.width && img.height) {
    if (it != tex_.end() && it->second.gl) glDeleteTextures(1, &it->second.gl);
    glGenTextures(1, &t.gl);
    glBindTexture(GL_TEXTURE_2D, t.gl);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<int>(img.width), static_cast<int>(img.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);  // pixel art: no smoothing
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    t.w = static_cast<int>(img.width);
    t.h = static_cast<int>(img.height);
  }
  Tex& slot = tex_[guid];
  slot = t;
  return slot.gl ? &slot : nullptr;
}

bool Scene2DResources::textureSize(const std::string& guid, int& w, int& h) {
  const Tex* t = texture(guid);
  if (!t) return false;
  w = t->w;
  h = t->h;
  return true;
}

const TilemapData* Scene2DResources::tilemap(const std::string& guid) {
  if (guid.empty() || !resolve_) return nullptr;
  if (live_) {
    if (const TilemapEdit* e = live_(guid)) {
      MapEntry& m = maps_[guid];
      if (!m.ok || m.live != static_cast<long long>(e->revision())) {
        m.data.tileset = e->tileset();
        m.data.tileSize = e->tileSize();
        m.data.width = e->width();
        m.data.height = e->height();
        m.data.tiles.assign(e->tiles().begin(), e->tiles().end());
        m.ok = true;
        m.live = static_cast<long long>(e->revision());
        m.stamp = 0;
      }
      return &m.data;
    }
  }
  auto it = maps_.find(guid);
  if (it != maps_.end() && !due("m:" + guid)) return it->second.ok ? &it->second.data : nullptr;
  const fs::path path = resolve_(guid);
  if (path.empty()) return nullptr;
  const long long stamp = stampOf(path);
  if (it != maps_.end() && it->second.stamp == stamp) return it->second.ok ? &it->second.data : nullptr;
  MapEntry e;
  e.stamp = stamp;
  e.live = -1;
  e.ok = readTilemap(path, e.data);
  MapEntry& slot = maps_[guid];
  slot = std::move(e);
  return slot.ok ? &slot.data : nullptr;
}

void Scene2DResources::clear() {
  for (auto& kv : tex_) if (kv.second.gl) glDeleteTextures(1, &kv.second.gl);
  tex_.clear();
  maps_.clear();
  checked_.clear();
}

std::vector<SpriteQuad> Scene2DResources::build(const Scene2D& scene, const std::map<std::string, unsigned>& ids) {
  struct Item { int sorting; size_t order; SpriteQuad q; };
  std::vector<Item> items;
  size_t order = 0;
  auto idOf = [&](const std::string& g) { auto it = ids.find(g); return it == ids.end() ? 0u : it->second; };
  const TextureSizeFn sizeFn = [this](const std::string& g, int& w, int& h) { return textureSize(g, w, h); };

  for (const TileLayer2D& layer : scene.layers) {
    const TilemapData* tm = tilemap(layer.map);
    if (!tm) continue;
    const Tex* ts = texture(tm->tileset);
    if (!ts || tm->tileSize <= 0) continue;
    const int cols = std::max(1, ts->w / tm->tileSize);
    const unsigned id = idOf(layer.guid);
    const float s = static_cast<float>(tm->tileSize);
    for (int y = 0; y < tm->height; ++y)
      for (int x = 0; x < tm->width; ++x) {
        const int t = tm->tiles[static_cast<size_t>(y) * static_cast<size_t>(tm->width) + static_cast<size_t>(x)];
        if (t < 0 || t >= 65535) continue;
        SpriteQuad q;
        q.tex = ts->gl;
        q.id = id;
        const float c[4][3] = {{x * s, y * s, 0}, {(x + 1) * s, y * s, 0}, {(x + 1) * s, (y + 1) * s, 0}, {x * s, (y + 1) * s, 0}};
        for (int i = 0; i < 4; ++i) m4Point(layer.model, c[i], q.p[i]);
        const float u0 = static_cast<float>((t % cols) * tm->tileSize) / static_cast<float>(ts->w);
        const float v0 = static_cast<float>((t / cols) * tm->tileSize) / static_cast<float>(ts->h);
        const float u1 = u0 + s / static_cast<float>(ts->w), v1 = v0 + s / static_cast<float>(ts->h);
        const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
        for (int i = 0; i < 4; ++i) { q.uv[i][0] = uv[i][0]; q.uv[i][1] = uv[i][1]; }
        for (int i = 0; i < 4; ++i) q.color[i] = layer.tint[i];
        items.push_back({layer.sorting, order++, q});
      }
  }
  for (const Sprite2D& sp : scene.sprites) {
    float corners[12];
    if (!spriteWorldCorners(sp, sizeFn, corners)) continue;
    SpriteQuad q;
    q.id = idOf(sp.guid);
    for (int i = 0; i < 4; ++i) for (int k = 0; k < 3; ++k) q.p[i][k] = corners[i * 3 + k];
    for (int i = 0; i < 4; ++i) q.color[i] = sp.color[i];
    const Tex* t = texture(sp.texture);
    if (t) {
      q.tex = t->gl;
      const int sw = sp.srcW > 0 ? sp.srcW : t->w, sh = sp.srcH > 0 ? sp.srcH : t->h;
      float u0 = static_cast<float>(sp.srcX) / static_cast<float>(t->w), u1 = static_cast<float>(sp.srcX + sw) / static_cast<float>(t->w);
      const float v0 = static_cast<float>(sp.srcY) / static_cast<float>(t->h), v1 = static_cast<float>(sp.srcY + sh) / static_cast<float>(t->h);
      if (sp.flipX) std::swap(u0, u1);
      const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
      for (int i = 0; i < 4; ++i) { q.uv[i][0] = uv[i][0]; q.uv[i][1] = uv[i][1]; }
    } else {
      q.color[3] *= 0.5f;  // missing texture: translucent rectangle
    }
    items.push_back({sp.sorting, order++, q});
  }
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.sorting < b.sorting; });
  std::vector<SpriteQuad> out;
  out.reserve(items.size());
  for (Item& i : items) out.push_back(i.q);
  return out;
}

}  // namespace editor
}  // namespace rensga
