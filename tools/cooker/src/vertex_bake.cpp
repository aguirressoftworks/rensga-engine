#include "vertex_bake.h"

#include "rensga/core/lighting.h"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>

namespace rensga {
namespace cooker {

namespace {

struct V3 { float x, y, z; };
inline V3 mk(const Vector3& v) { return {v.x, v.y, v.z}; }
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 norm(V3 a) {
  const float l = std::sqrt(dot(a, a));
  return l > 1e-12f ? a * (1.0f / l) : V3{0, 1, 0};
}

// Möller-Trumbore, two sided. Returns the hit distance in `t`.
bool rayTri(V3 o, V3 d, V3 a, V3 b, V3 c, float maxT, float& t) {
  const V3 e1 = b - a, e2 = c - a, p = cross(d, e2);
  const float det = dot(e1, p);
  if (std::fabs(det) < 1e-12f) return false;
  const float inv = 1.0f / det;
  const V3 s = o - a;
  const float u = dot(s, p) * inv;
  if (u < 0 || u > 1) return false;
  const V3 q = cross(s, e1);
  const float v = dot(d, q) * inv;
  if (v < 0 || u + v > 1) return false;
  t = dot(e2, q) * inv;
  return t > 1e-4f && t < maxT;
}

bool rayBox(V3 o, V3 d, const float mn[3], const float mx[3], float maxT) {
  float t0 = 0, t1 = maxT;
  const float oo[3] = {o.x, o.y, o.z}, dd[3] = {d.x, d.y, d.z};
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(dd[i]) < 1e-12f) {
      if (oo[i] < mn[i] || oo[i] > mx[i]) return false;
      continue;
    }
    float a = (mn[i] - oo[i]) / dd[i], b = (mx[i] - oo[i]) / dd[i];
    if (a > b) std::swap(a, b);
    t0 = std::max(t0, a);
    t1 = std::min(t1, b);
    if (t0 > t1) return false;
  }
  return true;
}

// Fixed cosine-weighted hemisphere pattern: the sample i of n (Hammersley points), z up.
void hemisphere(int i, int n, float& x, float& y, float& z) {
  // radical inverse base 2
  unsigned bits = static_cast<unsigned>(i);
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  const float u2 = static_cast<float>(bits) * 2.3283064365386963e-10f;
  const float u1 = (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
  const float r = std::sqrt(u1), ph = 6.28318530718f * u2;
  x = r * std::cos(ph);
  y = r * std::sin(ph);
  z = std::sqrt(std::max(0.0f, 1.0f - u1));
}

float num(const nlohmann::json& f, const char* key, float def) {
  auto it = f.find(key);
  return it != f.end() && it->is_number() ? it->get<float>() : def;
}
bool arr3(const nlohmann::json& f, const char* key, float out[3]) {
  auto it = f.find(key);
  if (it == f.end() || !it->is_array() || it->size() < 3) return false;
  for (int i = 0; i < 3; ++i) out[i] = (*it)[static_cast<size_t>(i)].is_number() ? (*it)[static_cast<size_t>(i)].get<float>() : 0;
  return true;
}
bool color3(const nlohmann::json& f, const char* key, float out[3]) {  // Color32 as [r,g,b,a] 0..255
  float v[3];
  if (!arr3(f, key, v)) return false;
  for (int i = 0; i < 3; ++i) out[i] = v[i] / 255.0f;
  return true;
}

}  // namespace

void VertexBaker::addOccluder(const std::vector<Vector3>& world) {
  if (world.size() < 3) return;
  const size_t tris = world.size() / 3;
  for (size_t first = 0; first < tris; first += kChunkTris) {
    const size_t last = std::min(tris, first + kChunkTris);
    Occ o;
    o.tris.assign(world.begin() + static_cast<long>(first * 3), world.begin() + static_cast<long>(last * 3));
    for (int k = 0; k < 3; ++k) { o.bmin[k] = 1e30f; o.bmax[k] = -1e30f; }
    for (const Vector3& v : o.tris) {
      const float p[3] = {v.x, v.y, v.z};
      for (int k = 0; k < 3; ++k) { o.bmin[k] = std::min(o.bmin[k], p[k]); o.bmax[k] = std::max(o.bmax[k], p[k]); }
    }
    occ_.push_back(std::move(o));
  }
}

namespace {
struct Fnv {
  uint64_t h = 1469598103934665603ull;
  void bytes(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
  }
  template <class T> void add(const T& v) { bytes(&v, sizeof v); }
};

std::mutex g_mu;
std::map<uint64_t, std::vector<uint8_t>> g_cache;
std::atomic<uint64_t> g_planned{0}, g_done{0};
BakeStats g_stats;
thread_local BakeStats t_stats;
std::mutex g_gate;
}  // namespace

void VertexBaker::finalizeKey() {
  Fnv f;
  f.add(set_.ambient[0]); f.add(set_.ambient[1]); f.add(set_.ambient[2]);
  f.add(set_.shadows); f.add(set_.aoStrength); f.add(set_.aoSamples); f.add(set_.aoDistance); f.add(set_.bias);
  for (const BakeLightIn& l : lights_) {
    f.add(l.type); f.add(l.pos.x); f.add(l.pos.y); f.add(l.pos.z); f.add(l.dir.x); f.add(l.dir.y); f.add(l.dir.z);
    f.add(l.rgb[0]); f.add(l.rgb[1]); f.add(l.rgb[2]); f.add(l.range);
  }
  for (const Occ& o : occ_) f.bytes(o.tris.data(), o.tris.size() * sizeof(Vector3));
  key_ = f.h;
}

void bakeProgressBegin(uint64_t planned) {
  g_planned = planned;
  g_done = 0;
  std::lock_guard<std::mutex> lk(g_mu);
  g_stats = BakeStats();
}
uint64_t bakeVerticesPlanned() { return g_planned; }
uint64_t bakeVerticesDone() { return g_done; }
BakeStats lastBakeStatsThisThread() { return t_stats; }
BakeStats lastBakeStats() {
  std::lock_guard<std::mutex> lk(g_mu);
  return g_stats;
}
void bakeCacheClear() {
  std::lock_guard<std::mutex> lk(g_mu);
  g_cache.clear();
}

size_t VertexBaker::triangleCount() const {
  size_t n = 0;
  for (const Occ& o : occ_) n += o.tris.size() / 3;
  return n;
}

bool VertexBaker::occluded(const Vector3& o, const Vector3& d, float maxT) const {
  ++rays_;
  const V3 po = mk(o), pd = mk(d);
  for (const Occ& oc : occ_) {
    if (!rayBox(po, pd, oc.bmin, oc.bmax, maxT)) continue;
    for (size_t t = 0; t + 2 < oc.tris.size(); t += 3) {
      float th;
      if (rayTri(po, pd, mk(oc.tris[t]), mk(oc.tris[t + 1]), mk(oc.tris[t + 2]), maxT, th)) return true;
    }
  }
  return false;
}

void VertexBaker::bake(const Vector3* pos, const Vector3* normals, size_t n, const Matrix4& world, bool normalsAreWorld, uint8_t* out) const {
  // normal matrix: cofactors of the upper 3x3 (inverse transpose up to a positive factor), sign kept for mirrored scales
  const Scalar* m = world.m;
  const V3 c0{m[0], m[1], m[2]}, c1{m[4], m[5], m[6]}, c2{m[8], m[9], m[10]};
  const V3 k0 = cross(c1, c2), k1 = cross(c2, c0), k2 = cross(c0, c1);
  const float det = dot(c0, k0);
  const float sgn = det < 0 ? -1.0f : 1.0f;
  const int samples = std::max(1, std::min(64, set_.aoSamples));
  for (size_t i = 0; i < n; ++i) {
    const Vector3 wp = world.transformPoint(pos[i]);
    V3 wn;
    if (normalsAreWorld) wn = norm(mk(normals[i]));
    else {
      const V3 ln = mk(normals[i]);
      wn = norm((k0 * ln.x + k1 * ln.y + k2 * ln.z) * sgn);
    }
    const Vector3 origin(wp.x + wn.x * set_.bias, wp.y + wn.y * set_.bias, wp.z + wn.z * set_.bias);
    // ambient occlusion
    float ao = 1.0f;
    if (set_.aoStrength > 0 && !occ_.empty()) {
      const V3 up = std::fabs(wn.y) < 0.99f ? V3{0, 1, 0} : V3{1, 0, 0};
      const V3 tx = norm(cross(up, wn)), ty = cross(wn, tx);
      int hit = 0;
      for (int s = 0; s < samples; ++s) {
        float hx, hy, hz;
        hemisphere(s, samples, hx, hy, hz);
        const V3 d = tx * hx + ty * hy + wn * hz;
        if (occluded(origin, Vector3(d.x, d.y, d.z), set_.aoDistance)) ++hit;
      }
      ao = 1.0f - set_.aoStrength * static_cast<float>(hit) / static_cast<float>(samples);
    }
    float rgb[3] = {set_.ambient[0] * ao, set_.ambient[1] * ao, set_.ambient[2] * ao};
    for (const BakeLightIn& L : lights_) {
      V3 toL;
      float atten = 1.0f, maxT = 1e4f;
      if (L.type == 0) {
        toL = norm(V3{-L.dir.x, -L.dir.y, -L.dir.z});
      } else {
        const V3 d = V3{L.pos.x, L.pos.y, L.pos.z} - V3{wp.x, wp.y, wp.z};
        const float dist = std::sqrt(dot(d, d));
        // The same curve as the runtime renderers (lighting.h): 1 / (1 + K (d / range)^2), still 6 % at `range`. The runtime drops a
        // light beyond range + the object's radius; the bake has no object radius, so it stops at twice the range (1.6 %).
        if (L.range <= 0 || dist >= 2.0f * L.range) continue;
        atten = static_cast<float>(pointLightFalloff(dist, L.range));
        toL = dist > 1e-6f ? d * (1.0f / dist) : V3{0, 1, 0};
        maxT = dist - set_.bias;
      }
      const float ndl = dot(wn, toL);
      if (ndl <= 0) continue;
      if (set_.shadows && !occ_.empty() && occluded(origin, Vector3(toL.x, toL.y, toL.z), maxT)) continue;
      for (int k = 0; k < 3; ++k) rgb[k] += L.rgb[k] * atten * ndl;
    }
    for (int k = 0; k < 3; ++k) {
      const float v = std::max(0.0f, std::min(1.0f, rgb[k]));
      out[i * 4 + static_cast<size_t>(k)] = static_cast<uint8_t>(v * 255.0f + 0.5f);
    }
    out[i * 4 + 3] = 255;
    if ((i & 63) == 63) g_done += 64;
  }
  g_done += n & 63;
}

std::unique_ptr<VertexBakeScene> VertexBakeScene::create(const std::vector<BakeObjectIn>& objs, const BakeMeshLoader& load, std::vector<std::string>& errors) {
  bool any = false;
  for (const BakeObjectIn& o : objs)
    if (o.active)
      for (const BakeObjectIn::Comp& c : o.comps)
        if (c.type == "MeshRenderer" && wants(c.fields)) any = true;
  if (!any) return nullptr;
  std::unique_ptr<VertexBakeScene> s(new VertexBakeScene());
  s->gate_ = std::unique_lock<std::mutex>(g_gate);  // until this scene is destroyed: the end of the cook
  // world matrices (parents may follow children; depth capped)
  s->world_.assign(objs.size(), Matrix4::identity());
  std::vector<char> done(objs.size(), 0);
  std::function<void(size_t, int)> resolve = [&](size_t i, int depth) {
    if (done[i]) return;
    const BakeObjectIn& o = objs[i];
    Matrix4 local = Matrix4::TRS(Vector3(o.pos[0], o.pos[1], o.pos[2]), Quaternion(o.rot[0], o.rot[1], o.rot[2], o.rot[3]),
                                 Vector3(o.scale[0], o.scale[1], o.scale[2]));
    if (o.parent >= 0 && static_cast<size_t>(o.parent) < objs.size() && depth < 64) {
      resolve(static_cast<size_t>(o.parent), depth + 1);
      local = s->world_[static_cast<size_t>(o.parent)] * local;
    }
    s->world_[i] = local;
    done[i] = 1;
  };
  for (size_t i = 0; i < objs.size(); ++i) resolve(i, 0);

  BakeSettings set;
  bool haveSettings = false, anyLight = false;
  for (size_t i = 0; i < objs.size(); ++i) {
    if (!objs[i].active) continue;
    for (const BakeObjectIn::Comp& c : objs[i].comps) {
      if (c.type == "RenderSettings" && !haveSettings) {
        haveSettings = true;
        float amb[3];
        if (color3(c.fields, "ambient", amb)) for (int k = 0; k < 3; ++k) set.ambient[k] = amb[k];
        else for (int k = 0; k < 3; ++k) set.ambient[k] = 64.0f / 255.0f;  // RenderSettings default (64,64,64)
      }
    }
  }
  s->baker_ = VertexBaker(set);
  const Matrix4* W = s->world_.data();
  for (size_t i = 0; i < objs.size(); ++i) {
    if (!objs[i].active) continue;
    for (const BakeObjectIn::Comp& c : objs[i].comps) {
      if (c.type != "Light") continue;
      anyLight = true;
      BakeLightIn L;
      L.type = c.fields.value("type", 0) == 1 ? 1 : 0;
      float col[3] = {1, 1, 1};
      color3(c.fields, "color", col);
      const float inten = num(c.fields, "intensity", 1.0f);
      for (int k = 0; k < 3; ++k) L.rgb[k] = col[k] * inten;
      L.range = num(c.fields, "range", 10.0f);
      L.pos = W[i].translationPart();
      const Vector3 f = W[i].transformVector(Vector3(0, 0, 1));
      const float len = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
      L.dir = len > 1e-9f ? Vector3(f.x / len, f.y / len, f.z / len) : Vector3(0, -1, 0);
      s->baker_.addLight(L);
    }
  }
  if (!anyLight) {  // the old DirectionalLight, as the runtime does
    for (size_t i = 0; i < objs.size(); ++i) {
      if (!objs[i].active) continue;
      for (const BakeObjectIn::Comp& c : objs[i].comps) {
        if (c.type != "DirectionalLight") continue;
        BakeLightIn L;
        L.type = 0;
        float d[3] = {-0.4f, -1.0f, -0.3f};
        arr3(c.fields, "direction", d);
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        L.dir = len > 1e-9f ? Vector3(d[0] / len, d[1] / len, d[2] / len) : Vector3(0, -1, 0);
        color3(c.fields, "color", L.rgb);
        s->baker_.addLight(L);
      }
    }
  }
  // occluders: every active static MeshRenderer (skinned meshes move, so they cast nothing)
  std::map<std::string, std::pair<std::vector<Vector3>, std::vector<Vector3>>> cache;
  std::set<std::string> failed;
  for (size_t i = 0; i < objs.size(); ++i) {
    if (!objs[i].active) continue;
    for (const BakeObjectIn::Comp& c : objs[i].comps) {
      if (c.type != "MeshRenderer") continue;
      const std::string g = c.fields.value("mesh", std::string());
      if (g.empty() || failed.count(g)) continue;
      auto it = cache.find(g);
      if (it == cache.end()) {
        std::vector<Vector3> p, n;
        std::string err;
        if (!load(g, p, n, err)) {
          failed.insert(g);
          errors.push_back("vertex bake: mesh " + g + " does not load: " + err);
          continue;
        }
        it = cache.emplace(g, std::make_pair(std::move(p), std::move(n))).first;
      }
      std::vector<Vector3> wv;
      wv.reserve(it->second.first.size());
      for (const Vector3& p : it->second.first) wv.push_back(W[i].transformPoint(p));
      s->baker_.addOccluder(wv);
    }
  }
  s->baker_.finalizeKey();
  uint64_t planned = 0;
  for (size_t i = 0; i < objs.size(); ++i) {
    if (!objs[i].active) continue;
    for (const BakeObjectIn::Comp& c : objs[i].comps)
      if (c.type == "MeshRenderer" && wants(c.fields)) {
        auto it = cache.find(c.fields.value("mesh", std::string()));
        if (it != cache.end()) planned += it->second.first.size();
      }
  }
  bakeProgressBegin(planned);
  return s;
}

VertexBakeScene::~VertexBakeScene() {
  std::lock_guard<std::mutex> lk(g_mu);
  t_stats = g_stats;
}

void VertexBakeScene::bakeObject(size_t i, const std::vector<Vector3>& pos, const std::vector<Vector3>& nrm, bool normalsAreWorld,
                                 std::vector<uint8_t>& colors) const {
  colors.assign(pos.size() * 4, 255);
  if (pos.empty() || pos.size() != nrm.size() || i >= world_.size()) return;
  const auto t0 = std::chrono::steady_clock::now();
  Fnv f;
  f.add(baker_.key());
  f.add(normalsAreWorld);
  f.bytes(world_[i].m, sizeof(Scalar) * 16);
  f.bytes(pos.data(), pos.size() * sizeof(Vector3));
  f.bytes(nrm.data(), nrm.size() * sizeof(Vector3));
  const uint64_t key = f.h;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_cache.find(key);
    if (it != g_cache.end() && it->second.size() == colors.size()) {
      colors = it->second;
      g_done += pos.size();
      g_stats.meshes++;
      g_stats.vertices += pos.size();
      g_stats.cacheHits++;
      ++bakedObjects_;
      return;
    }
  }
  const uint64_t rays0 = baker_.raysCast();
  baker_.bake(pos.data(), nrm.data(), pos.size(), world_[i], normalsAreWorld, colors.data());
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_cache.size() > 256) g_cache.clear();
    g_cache[key] = colors;
    g_stats.meshes++;
    g_stats.vertices += pos.size();
    g_stats.rays += baker_.raysCast() - rays0;
    g_stats.ms += ms;
  }
  ++bakedObjects_;
}

}  // namespace cooker
}  // namespace rensga
