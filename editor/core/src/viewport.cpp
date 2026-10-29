#include "rensga/editor/viewport.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include "rensga/cooker/cooker.h"

namespace rensga {
namespace editor {
namespace fs = std::filesystem;

// ---- matrices ------------------------------------------------------------------------------------
M4 m4Identity() { M4 r{}; r[0] = r[5] = r[10] = r[15] = 1; return r; }

M4 m4Mul(const M4& a, const M4& b) {
  M4 r{};
  for (int c = 0; c < 4; ++c)
    for (int rr = 0; rr < 4; ++rr) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a[k * 4 + rr] * b[c * 4 + k];
      r[c * 4 + rr] = s;
    }
  return r;
}

M4 m4FromDouble(const double m[16]) {
  M4 r;
  for (int i = 0; i < 16; ++i) r[i] = static_cast<float>(m[i]);
  return r;
}

M4 m4Invert(const M4& mm) {
  const float* m = mm.data();
  float inv[16];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (std::fabs(det) < 1e-20f) return m4Identity();
  M4 r;
  for (int i = 0; i < 16; ++i) r[i] = inv[i] / det;
  return r;
}

void m4Point(const M4& m, const float p[3], float o[3]) {
  float w = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
  if (std::fabs(w) < 1e-12f) w = 1;
  o[0] = (m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12]) / w;
  o[1] = (m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13]) / w;
  o[2] = (m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14]) / w;
}

static float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

M4 viewLH(const float e[3], const float r[3], const float u[3], const float f[3]) {
  M4 m = m4Identity();
  m[0] = r[0]; m[4] = r[1]; m[8] = r[2];  m[12] = -dot3(r, e);
  m[1] = u[0]; m[5] = u[1]; m[9] = u[2];  m[13] = -dot3(u, e);
  m[2] = f[0]; m[6] = f[1]; m[10] = f[2]; m[14] = -dot3(f, e);
  return m;
}

M4 perspectiveLH(float fovYDeg, float aspect, float n, float f) {
  float t = 1.0f / std::tan(fovYDeg * 0.5f * 3.14159265f / 180.0f);
  M4 m{};
  m[0] = t / aspect; m[5] = t;
  m[10] = (f + n) / (f - n); m[11] = 1; m[14] = -2 * f * n / (f - n);
  return m;
}

M4 viewToRightHanded(const M4& v) { M4 r = v; r[2] = -r[2]; r[6] = -r[6]; r[10] = -r[10]; r[14] = -r[14]; return r; }
M4 projToRightHanded(const M4& p) { M4 r = p; for (int i = 8; i < 12; ++i) r[i] = -r[i]; return r; }

// ---- editor camera -------------------------------------------------------------------------------
void EditorCamera::forward(float f[3]) const {
  f[0] = std::cos(pitch) * std::sin(yaw); f[1] = std::sin(pitch); f[2] = std::cos(pitch) * std::cos(yaw);
}
void EditorCamera::right(float r[3]) const { r[0] = std::cos(yaw); r[1] = 0; r[2] = -std::sin(yaw); }
void EditorCamera::up(float u[3]) const {
  float f[3], r[3];
  forward(f); right(r);
  u[0] = f[1] * r[2] - f[2] * r[1]; u[1] = f[2] * r[0] - f[0] * r[2]; u[2] = f[0] * r[1] - f[1] * r[0];
}
void EditorCamera::eye(float e[3]) const {
  float f[3];
  forward(f);
  for (int i = 0; i < 3; ++i) e[i] = pivot[i] - f[i] * distance;
}
M4 EditorCamera::view() const {
  float e[3], r[3], u[3], f[3];
  eye(e); right(r); up(u); forward(f);
  return viewLH(e, r, u, f);
}

static const float kMaxPitch = 1.5533f;  // 89 degrees
void EditorCamera::look(float dx, float dy) {
  float e[3];
  eye(e);
  yaw += dx * 0.0035f;
  pitch = std::clamp(pitch - dy * 0.0035f, -kMaxPitch, kMaxPitch);
  float f[3];
  forward(f);
  for (int i = 0; i < 3; ++i) pivot[i] = e[i] + f[i] * distance;
}
void EditorCamera::orbit(float dx, float dy) {
  yaw += dx * 0.005f;
  pitch = std::clamp(pitch - dy * 0.005f, -kMaxPitch, kMaxPitch);
}
void EditorCamera::pan(float dx, float dy) {
  float r[3], u[3];
  right(r); up(u);
  float k = distance * 0.0016f * (fovDeg / 60.0f);
  for (int i = 0; i < 3; ++i) pivot[i] += -r[i] * dx * k + u[i] * dy * k;
}
void EditorCamera::zoom(float wheel) { distance = std::clamp(distance * std::pow(0.88f, wheel), 0.05f, farClip * 0.5f); }
void EditorCamera::fly(float rt, float upv, float fw, float units) {
  float f[3], r[3];
  forward(f); right(r);
  for (int i = 0; i < 3; ++i) pivot[i] += (r[i] * rt + f[i] * fw) * units;
  pivot[1] += upv * units;
}
void EditorCamera::setDirection(const float f[3]) {
  float l = std::sqrt(dot3(f, f));
  if (l < 1e-6f) return;
  float e[3];
  eye(e);
  yaw = std::atan2(f[0], f[2]);
  pitch = std::clamp(std::asin(std::clamp(f[1] / l, -1.0f, 1.0f)), -kMaxPitch, kMaxPitch);
  float nf[3];
  forward(nf);
  for (int i = 0; i < 3; ++i) pivot[i] = e[i] + nf[i] * distance;
}
void EditorCamera::frame(const float c[3], float radius) {
  for (int i = 0; i < 3; ++i) pivot[i] = c[i];
  float half = std::tan(fovDeg * 0.5f * 3.14159265f / 180.0f);
  distance = std::clamp(std::max(radius, 0.25f) / half * 1.15f, 0.5f, farClip * 0.5f);
}

bool EditorCamera::load(const fs::path& file) {
  try {
    std::ifstream in(file);
    if (!in) return false;
    nlohmann::json j = nlohmann::json::parse(in);
    auto p = j.at("pivot");
    for (int i = 0; i < 3; ++i) pivot[i] = p.at(i).get<float>();
    yaw = j.at("yaw").get<float>();
    pitch = std::clamp(j.at("pitch").get<float>(), -kMaxPitch, kMaxPitch);
    distance = std::clamp(j.at("distance").get<float>(), 0.05f, 100000.0f);
    return true;
  } catch (...) { return false; }
}
bool EditorCamera::save(const fs::path& file) const {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  nlohmann::ordered_json j;
  j["pivot"] = {pivot[0], pivot[1], pivot[2]};
  j["yaw"] = yaw; j["pitch"] = pitch; j["distance"] = distance;
  std::ofstream out(file);
  if (!out) return false;
  out << j.dump(1) << "\n";
  return true;
}

// ---- meshes --------------------------------------------------------------------------------------
void MeshLibrary::setProject(const fs::path& root) {
  root_ = root;
  files_.clear();
  shapes_.clear();
  cache_.clear();
  tess_ = Tess();
  try {
    ConsoleTargetData t = loadConsoleTarget(root / "ProjectSettings" / "ConsoleTarget.json");
    tess_.sphere = t.sphereSegments; tess_.capsule = t.capsuleSegments;
  } catch (...) {}
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root / "Assets", ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path& p = it->path();
    std::string n = p.filename().string();
    if (n.size() < 15 || n.compare(n.size() - 15, 15, ".prim.json.meta") != 0) continue;
    try {
      std::ifstream in(p);
      nlohmann::json j = nlohmann::json::parse(in);
      fs::path asset = p;
      asset.replace_extension("");  // drop .meta
      files_[j.at("guid").get<std::string>()] = asset;
      try {
        std::ifstream ain(asset);
        nlohmann::json aj = nlohmann::json::parse(ain);
        std::string shape = aj.value("shape", "");
        if (!shape.empty() && !shapes_.count(shape)) shapes_[shape] = j.at("guid").get<std::string>();
      } catch (...) {}
    } catch (...) {}
  }
}

std::string MeshLibrary::guidForShape(const std::string& shape) const {
  auto it = shapes_.find(shape);
  return it == shapes_.end() ? std::string() : it->second;
}

std::shared_ptr<const MeshData> MeshLibrary::get(const std::string& guid) {
  auto c = cache_.find(guid);
  if (c != cache_.end()) return c->second;
  std::shared_ptr<const MeshData> out;
  auto f = files_.find(guid);
  if (f != files_.end()) {
    try {
      std::ifstream in(f->second, std::ios::binary);
      std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      PrimitiveParams pp;
      std::string err;
      if (parsePrimitive(text, pp, err)) {
        ConsoleTargetData td;
        td.sphereSegments = tess_.sphere; td.capsuleSegments = tess_.capsule;
        PrimitiveMesh pm = generatePrimitive(pp, td);
        auto md = std::make_shared<MeshData>();
        for (size_t i = 0; i < pm.positions.size(); ++i) {
          const Vector3& p = pm.positions[i];
          const Vector3& n = pm.normals[i];
          const float pv[3] = {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)};
          md->pos.insert(md->pos.end(), pv, pv + 3);
          md->nrm.push_back(static_cast<float>(n.x)); md->nrm.push_back(static_cast<float>(n.y)); md->nrm.push_back(static_cast<float>(n.z));
          for (int k = 0; k < 3; ++k) {
            if (i == 0 || pv[k] < md->bmin[k]) md->bmin[k] = pv[k];
            if (i == 0 || pv[k] > md->bmax[k]) md->bmax[k] = pv[k];
          }
        }
        out = md;
      }
    } catch (...) {}
  }
  cache_[guid] = out;
  return out;
}

// ---- scene ---------------------------------------------------------------------------------------
SceneDraw gatherScene(const SceneDocument& doc, MeshLibrary& lib) {
  SceneDraw sd;
  for (const SceneObject& o : doc.scene().objects) {
    if (!o.active) continue;
    double wd[16];
    doc.worldMatrix(o.guid, wd);
    M4 w = m4FromDouble(wd);
    const SceneComponent* mr = nullptr;
    for (const SceneComponent& c : o.components) {
      if (c.type == "MeshRenderer" && !mr) mr = &c;
      else if (c.type == "DirectionalLight") {
        const auto& f = c.fields;
        try {
          if (f.contains("direction")) for (int i = 0; i < 3; ++i) sd.light.dir[i] = f["direction"].at(i).get<float>();
          if (f.contains("color")) for (int i = 0; i < 3; ++i) sd.light.color[i] = f["color"].at(i).get<float>() / 255.0f;
        } catch (...) {}
      } else if (c.type == "Camera" && !sd.camera.found) {
        CameraInfo& ci = sd.camera;
        ci.found = true; ci.guid = o.guid;
        ci.fovDeg = c.fields.value("fov", 60.0f);
        ci.nearClip = c.fields.value("nearClip", 0.3f);
        ci.farClip = c.fields.value("farClip", 200.0f);
        auto norm = [&](const float* a, float* b) {
          float l = std::sqrt(dot3(a, a));
          for (int i = 0; i < 3; ++i) b[i] = l > 1e-8f ? a[i] / l : 0;
        };
        const float rx[3] = {w[0], w[1], w[2]}, uy[3] = {w[4], w[5], w[6]}, fz[3] = {w[8], w[9], w[10]};
        norm(rx, ci.right); norm(uy, ci.up); norm(fz, ci.fwd);
        ci.eye[0] = w[12]; ci.eye[1] = w[13]; ci.eye[2] = w[14];
      }
    }
    std::shared_ptr<const MeshData> mesh;
    if (mr && mr->fields.contains("mesh") && mr->fields["mesh"].is_string()) mesh = lib.get(mr->fields["mesh"].get<std::string>());
    if (mesh) {
      Drawable d;
      d.guid = o.guid; d.model = w; d.mesh = mesh;
      try {
        if (mr->fields.contains("color")) for (int i = 0; i < 4; ++i) d.color[i] = mr->fields["color"].at(i).get<float>() / 255.0f;
      } catch (...) {}
      sd.drawables.push_back(std::move(d));
    } else {
      Marker m;
      m.guid = o.guid; m.model = w;
      m.kind = o.components.empty() ? "Empty" : o.components[0].type;
      for (const SceneComponent& c : o.components)
        if (c.type == "Camera" || c.type == "DirectionalLight") { m.kind = c.type; break; }
      sd.markers.push_back(std::move(m));
    }
  }
  return sd;
}

Ray rayFromScreen(const M4& view, const M4& proj, float nx, float ny) {
  M4 inv = m4Invert(m4Mul(proj, view));
  float x = nx * 2 - 1, y = 1 - ny * 2;
  auto unproject = [&](float z, float out[3]) {
    float v[4] = {x, y, z, 1}, r[4];
    for (int i = 0; i < 4; ++i) r[i] = inv[i] * v[0] + inv[4 + i] * v[1] + inv[8 + i] * v[2] + inv[12 + i] * v[3];
    for (int i = 0; i < 3; ++i) out[i] = r[i] / r[3];
  };
  float a[3], b[3];
  unproject(-1, a); unproject(1, b);
  Ray ray;
  for (int i = 0; i < 3; ++i) { ray.o[i] = a[i]; ray.d[i] = b[i] - a[i]; }
  return ray;
}

static bool rayAabb(const float o[3], const float d[3], const float mn[3], const float mx[3]) {
  float t0 = 0, t1 = 1e30f;
  for (int i = 0; i < 3; ++i) {
    if (std::fabs(d[i]) < 1e-12f) { if (o[i] < mn[i] || o[i] > mx[i]) return false; continue; }
    float a = (mn[i] - o[i]) / d[i], b = (mx[i] - o[i]) / d[i];
    if (a > b) std::swap(a, b);
    t0 = std::max(t0, a); t1 = std::min(t1, b);
    if (t0 > t1) return false;
  }
  return true;
}

static bool rayTri(const float o[3], const float d[3], const float* a, const float* b, const float* c, float& t) {
  float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
  float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
  float det = dot3(e1, p);
  if (std::fabs(det) < 1e-12f) return false;
  float inv = 1 / det, s[3] = {o[0] - a[0], o[1] - a[1], o[2] - a[2]};
  float u = dot3(s, p) * inv;
  if (u < 0 || u > 1) return false;
  float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
  float v = dot3(d, q) * inv;
  if (v < 0 || u + v > 1) return false;
  t = dot3(e2, q) * inv;
  return t > 0;
}

std::string pickObject(const SceneDraw& sd, const Ray& ray, float* tOut) {
  std::string best;
  float bestT = 1e30f;
  for (const Drawable& dr : sd.drawables) {
    M4 inv = m4Invert(dr.model);
    float o[3], d[3];
    m4Point(inv, ray.o, o);
    for (int i = 0; i < 3; ++i) d[i] = inv[i] * ray.d[0] + inv[4 + i] * ray.d[1] + inv[8 + i] * ray.d[2];
    if (!rayAabb(o, d, dr.mesh->bmin, dr.mesh->bmax)) continue;
    const float* p = dr.mesh->pos.data();
    for (size_t t = 0; t < dr.mesh->triangleCount(); ++t) {
      float th;
      if (rayTri(o, d, p + t * 9, p + t * 9 + 3, p + t * 9 + 6, th) && th < bestT) { bestT = th; best = dr.guid; }
    }
  }
  for (const Marker& m : sd.markers) {
    const float c[3] = {m.model[12], m.model[13], m.model[14]}, r = 0.4f;
    float oc[3] = {ray.o[0] - c[0], ray.o[1] - c[1], ray.o[2] - c[2]};
    float a = dot3(ray.d, ray.d), b = 2 * dot3(oc, ray.d), cc = dot3(oc, oc) - r * r;
    float disc = b * b - 4 * a * cc;
    if (a < 1e-20f || disc < 0) continue;
    float t = (-b - std::sqrt(disc)) / (2 * a);
    if (t > 0 && t < bestT) { bestT = t; best = m.guid; }
  }
  if (tOut) *tOut = bestT;
  return best;
}

bool objectBounds(const SceneDraw& sd, const std::string& guid, float center[3], float* radius) {
  for (const Drawable& dr : sd.drawables) {
    if (dr.guid != guid) continue;
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (int k = 0; k < 8; ++k) {
      const float p[3] = {(k & 1) ? dr.mesh->bmax[0] : dr.mesh->bmin[0], (k & 2) ? dr.mesh->bmax[1] : dr.mesh->bmin[1],
                          (k & 4) ? dr.mesh->bmax[2] : dr.mesh->bmin[2]};
      float w[3];
      m4Point(dr.model, p, w);
      for (int i = 0; i < 3; ++i) { mn[i] = std::min(mn[i], w[i]); mx[i] = std::max(mx[i], w[i]); }
    }
    float h[3];
    for (int i = 0; i < 3; ++i) { center[i] = (mn[i] + mx[i]) * 0.5f; h[i] = (mx[i] - mn[i]) * 0.5f; }
    *radius = std::sqrt(dot3(h, h));
    return true;
  }
  for (const Marker& m : sd.markers) {
    if (m.guid != guid) continue;
    center[0] = m.model[12]; center[1] = m.model[13]; center[2] = m.model[14];
    *radius = 0.6f;
    return true;
  }
  return false;
}

}  // namespace editor
}  // namespace rensga
