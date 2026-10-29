// MVP-19: shared primitive generator. Flat-shaded triangle lists, CCW winding seen from outside.
// Tessellation comes from the ConsoleTarget; colliders match the generated bounds.
#include <math.h>

#include <nlohmann/json.hpp>

#include "rensga/cooker/cooker.h"

namespace rensga {

namespace {

struct Builder {
  PrimitiveMesh mesh;
  Vector3 center;  // inside the (convex) solid; used to orient triangles outward
  void tri(Vector3 a, Vector3 b, Vector3 c, bool forceUp = false) {
    Vector3 n = Vector3::Cross(b - a, c - a);
    if (n.sqrMagnitude() < 1e-12f) return;  // degenerate (poles)
    n = n.normalized();
    Vector3 mid = (a + b + c) / 3.0f;
    bool flip = forceUp ? n.y < 0 : Vector3::Dot(n, mid - center) < 0;
    if (flip) {
      Vector3 t = b; b = c; c = t;
      n = -n;
    }
    mesh.positions.push_back(a); mesh.positions.push_back(b); mesh.positions.push_back(c);
    mesh.normals.push_back(n); mesh.normals.push_back(n); mesh.normals.push_back(n);
  }
  void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, bool forceUp = false) {
    tri(a, b, c, forceUp);
    tri(a, c, d, forceUp);
  }
};

PrimitiveMesh makeBox(Vector3 half, Vector3 c) {
  Builder b;
  b.center = c;
  Vector3 h = half;
  Vector3 v[8];
  for (int i = 0; i < 8; ++i) v[i] = c + Vector3((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z);
  b.quad(v[0], v[1], v[3], v[2]);  // -z
  b.quad(v[4], v[5], v[7], v[6]);  // +z
  b.quad(v[0], v[2], v[6], v[4]);  // -x
  b.quad(v[1], v[3], v[7], v[5]);  // +x
  b.quad(v[0], v[1], v[5], v[4]);  // -y
  b.quad(v[2], v[3], v[7], v[6]);  // +y
  return b.mesh;
}

// Solid of revolution around Y from a profile of (radius, y) points, pole to pole.
PrimitiveMesh lathe(const std::vector<std::pair<Scalar, Scalar>>& profile, int lon) {
  Builder b;
  const Scalar twoPi = 6.28318530718f;
  auto ring = [&](size_t i, int j) {
    Scalar a = twoPi * static_cast<Scalar>(j % lon) / static_cast<Scalar>(lon);
    return Vector3(profile[i].first * cosf(a), profile[i].second, profile[i].first * sinf(a));
  };
  for (size_t i = 0; i + 1 < profile.size(); ++i)
    for (int j = 0; j < lon; ++j) b.quad(ring(i, j), ring(i, j + 1), ring(i + 1, j + 1), ring(i + 1, j));
  return b.mesh;
}

}  // namespace

PrimitiveMesh generatePrimitive(const PrimitiveParams& p, const ConsoleTargetData& t) {
  const Scalar pi = 3.14159265359f;
  switch (p.shape) {
    case PrimitiveShape::Cube:
      return makeBox(p.size * 0.5f, Vector3());
    case PrimitiveShape::Plane: {
      Builder b;
      Scalar hx = p.size.x * 0.5f, hz = p.size.z * 0.5f;
      Scalar cell = p.cellSize > 0 ? p.cellSize : p.size.x;
      int nx = static_cast<int>(p.size.x / cell + 0.999f), nz = static_cast<int>(p.size.z / cell + 0.999f);
      if (nx < 1) nx = 1;
      if (nz < 1) nz = 1;
      for (int i = 0; i < nx; ++i) {
        for (int j = 0; j < nz; ++j) {
          Scalar x0 = -hx + p.size.x * i / nx, x1 = -hx + p.size.x * (i + 1) / nx;
          Scalar z0 = -hz + p.size.z * j / nz, z1 = -hz + p.size.z * (j + 1) / nz;
          b.quad(Vector3(x0, 0, z0), Vector3(x0, 0, z1), Vector3(x1, 0, z1), Vector3(x1, 0, z0), true);
        }
      }
      return b.mesh;
    }
    case PrimitiveShape::Ramp: {
      // Wedge rising toward +z: low edge at z=-sz/2 (y=-sy/2), high wall at z=+sz/2.
      Builder b;
      Scalar x = p.size.x * 0.5f, y = p.size.y * 0.5f, z = p.size.z * 0.5f;
      b.center = Vector3(0, -y * 0.5f, 0);  // interior point (the origin lies on the slope plane)
      Vector3 A(-x, -y, -z), B(x, -y, -z), C(x, -y, z), D(-x, -y, z), E(-x, y, z), F(x, y, z);
      b.quad(A, B, C, D);  // bottom
      b.quad(C, F, E, D);  // back wall
      b.quad(A, B, F, E);  // slope
      b.tri(A, D, E);      // left side
      b.tri(B, F, C);      // right side
      return b.mesh;
    }
    case PrimitiveShape::Sphere: {
      int seg = t.sphereSegments < 4 ? 4 : t.sphereSegments;
      int rings = seg / 2;
      std::vector<std::pair<Scalar, Scalar>> prof;
      for (int i = 0; i <= rings; ++i) {
        Scalar th = pi * static_cast<Scalar>(i) / static_cast<Scalar>(rings);
        prof.push_back({p.radius * sinf(th), p.radius * cosf(th)});
      }
      return lathe(prof, seg);
    }
    case PrimitiveShape::Capsule: {
      int seg = t.capsuleSegments < 4 ? 4 : t.capsuleSegments;
      int k = seg / 4 < 2 ? 2 : seg / 4;
      Scalar h = p.height < 2 * p.radius ? 2 * p.radius : p.height;
      Scalar cy = h * 0.5f - p.radius;
      std::vector<std::pair<Scalar, Scalar>> prof;
      for (int i = 0; i <= k; ++i) {
        Scalar th = (pi * 0.5f) * static_cast<Scalar>(i) / static_cast<Scalar>(k);
        prof.push_back({p.radius * sinf(th), cy + p.radius * cosf(th)});
      }
      for (int i = 0; i <= k; ++i) {
        Scalar th = pi * 0.5f + (pi * 0.5f) * static_cast<Scalar>(i) / static_cast<Scalar>(k);
        prof.push_back({p.radius * sinf(th), -cy + p.radius * cosf(th)});
      }
      return lathe(prof, seg);
    }
  }
  return {};
}

PrimitiveCollider colliderForPrimitive(const PrimitiveParams& p) {
  PrimitiveCollider c;
  switch (p.shape) {
    case PrimitiveShape::Cube: c.size = p.size; break;
    case PrimitiveShape::Ramp: c.size = p.size; break;  // bounding box
    case PrimitiveShape::Plane:
      c.size = Vector3(p.size.x, 0.1f, p.size.z);
      c.center = Vector3(0, -0.05f, 0);  // top face at y=0
      break;
    case PrimitiveShape::Sphere: c.isSphere = true; c.radius = p.radius; break;
    case PrimitiveShape::Capsule:
      c.size = Vector3(2 * p.radius, p.height < 2 * p.radius ? 2 * p.radius : p.height, 2 * p.radius);
      break;
  }
  return c;
}

bool parsePrimitive(const std::string& text, PrimitiveParams& out, std::string& error) {
  using nlohmann::json;
  try {
    json j = json::parse(text);
    std::string shape = j.at("shape").get<std::string>();
    const json params = j.value("params", json::object());
    out = PrimitiveParams();
    if (shape == "cube") out.shape = PrimitiveShape::Cube;
    else if (shape == "sphere") out.shape = PrimitiveShape::Sphere;
    else if (shape == "capsule") out.shape = PrimitiveShape::Capsule;
    else if (shape == "plane") out.shape = PrimitiveShape::Plane;
    else if (shape == "ramp") out.shape = PrimitiveShape::Ramp;
    else { error = "unknown shape '" + shape + "'"; return false; }
    if (params.contains("size")) {
      const json& s = params["size"];
      if (out.shape == PrimitiveShape::Plane) {
        if (!s.is_array() || s.size() != 2) { error = "plane size must be [x, z]"; return false; }
        out.size = Vector3(s[0].get<float>(), 0, s[1].get<float>());
      } else {
        if (!s.is_array() || s.size() != 3) { error = "size must be [x, y, z]"; return false; }
        out.size = Vector3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>());
      }
    } else if (out.shape == PrimitiveShape::Plane) {
      out.size = Vector3(10, 0, 10);
    }
    if (params.contains("radius")) out.radius = params["radius"].get<float>();
    if (params.contains("height")) out.height = params["height"].get<float>();
    if (params.contains("cellSize")) out.cellSize = params["cellSize"].get<float>();
    return true;
  } catch (const std::exception& e) {
    error = e.what();
    return false;
  }
}

}  // namespace rensga
