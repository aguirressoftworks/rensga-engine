// F5.14 (cook side): lit mesh helpers (primitive and glTF -> LitMesh, winding and near-clip flags, C16 UV check).
#include <algorithm>
#include <cmath>

#include "rensga/cooker/model3d.h"

namespace rensga {

LitMesh litMeshFromPrimitive(const PrimitiveMesh& p) {
  LitMesh m;
  m.positions = p.positions;
  m.normals = p.normals;
  if (!p.colors.empty()) m.colors = p.colors;
  const size_t n = p.positions.size();
  m.uvs.assign(n * 2, 0.0f);
  if (n == 0) return m;
  Vector3 lo = p.positions[0], hi = p.positions[0];
  for (const Vector3& v : p.positions) {
    lo = Vector3(std::min(lo.x, v.x), std::min(lo.y, v.y), std::min(lo.z, v.z));
    hi = Vector3(std::max(hi.x, v.x), std::max(hi.y, v.y), std::max(hi.z, v.z));
  }
  auto norm = [](float v, float a, float b) { return b > a ? (v - a) / (b - a) : 0.0f; };
  for (size_t t = 0; t + 2 < n + 0 && t < n; t += 3) {
    const Vector3 fn = Vector3::Cross(p.positions[t + 1] - p.positions[t], p.positions[t + 2] - p.positions[t]);
    const float ax = std::fabs(fn.x), ay = std::fabs(fn.y), az = std::fabs(fn.z);
    for (size_t k = 0; k < 3; ++k) {
      const Vector3& v = p.positions[t + k];
      float u, w;
      if (ay >= ax && ay >= az) { u = norm(v.x, lo.x, hi.x); w = norm(v.z, lo.z, hi.z); }
      else if (az >= ax) { u = norm(v.x, lo.x, hi.x); w = 1.0f - norm(v.y, lo.y, hi.y); }
      else { u = norm(v.z, lo.z, hi.z); w = 1.0f - norm(v.y, lo.y, hi.y); }
      m.uvs[(t + k) * 2] = u;
      m.uvs[(t + k) * 2 + 1] = w;
    }
  }
  return m;
}

LitMesh litMeshFromGltf(const GltfModel& g) { return g.mesh; }

uint32_t analyzeMeshFlags(const LitMesh& m, std::string& note) {
  note.clear();
  uint32_t flags = 0;
  size_t ccw = 0, cw = 0;
  bool longEdge = false;
  const float maxEdge2 = kNearClipEdgeLength * kNearClipEdgeLength;
  for (size_t t = 0; t + 2 < m.positions.size() + 0 && t < m.positions.size(); t += 3) {
    const Vector3 &a = m.positions[t], &b = m.positions[t + 1], &c = m.positions[t + 2];
    const Vector3 cr = Vector3::Cross(b - a, c - a);
    if (cr.sqrMagnitude() > 1e-12f) {
      const Vector3 nrm = m.normals[t] + m.normals[t + 1] + m.normals[t + 2];
      const float d = Vector3::Dot(cr, nrm);
      if (d > 0) ++ccw;
      else if (d < 0) ++cw;
    }
    const float e0 = (b - a).sqrMagnitude(), e1 = (c - b).sqrMagnitude(), e2 = (a - c).sqrMagnitude();
    if (std::max(e0, std::max(e1, e2)) > maxEdge2) longEdge = true;
  }
  if (ccw > 0 && cw == 0) flags |= kMeshWindingCCW;
  else if (ccw > 0 && cw > 0)
    note = "triangle winding is mixed (" + std::to_string(ccw) + " counter-clockwise, " + std::to_string(cw) +
           " clockwise); the winding flag stays clear and back-face culling may drop faces";
  if (longEdge) flags |= kMeshNeedsNearClip;
  return flags;
}

bool uvFitsC16(const LitMesh& m) {
  for (float v : m.uvs)
    if (!(v >= 0.0f && v < 2.0f)) return false;
  return true;
}

}  // namespace rensga
