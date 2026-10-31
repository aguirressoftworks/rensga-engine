// F5.14 (cook side): splits a skinned mesh into batches of at most maxBones joints (GU weight i uses bone matrix i).
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

#include "rensga/cooker/model3d.h"

namespace rensga {

void quantizeWeights128(const float w[8], uint8_t out[8]) {
  float sum = 0;
  for (int i = 0; i < 8; ++i) sum += w[i] > 0 ? w[i] : 0;
  int q[8] = {};
  float frac[8] = {};
  int total = 0;
  for (int i = 0; i < 8; ++i) {
    const float v = (w[i] > 0 && sum > 0) ? w[i] / sum * 128.0f : 0.0f;
    q[i] = static_cast<int>(std::floor(v));
    frac[i] = v - static_cast<float>(q[i]);
    total += q[i];
  }
  while (total < 128) {  // largest remainder first, ties to the lower slot
    int best = 0;
    for (int i = 1; i < 8; ++i)
      if (frac[i] > frac[best]) best = i;
    ++q[best];
    frac[best] = -1.0f;
    ++total;
  }
  for (int i = 0; i < 8; ++i) out[i] = static_cast<uint8_t>(q[i]);
}

bool splitSkinnedMesh(const LitMesh& in, const std::vector<SkinInfluences>& infIn, uint32_t maxBonesIn, SkinSplitResult& out,
                      std::string& warning, std::string& error) {
  const size_t n = in.positions.size();
  if (n == 0 || n % 3 != 0 || infIn.size() != n || in.normals.size() != n) {
    error = "skinned mesh has inconsistent vertex arrays";
    return false;
  }
  const uint32_t maxBones = std::max(1u, std::min(maxBonesIn ? maxBonesIn : kMaxBatchBones, kMaxBatchBones));
  out = SkinSplitResult();
  warning.clear();
  std::vector<SkinInfluences> inf = infIn;
  const size_t tris = n / 3;

  // 1. Per triangle: if its joints exceed maxBones, drop the smallest influences (renormalized) until they fit.
  std::vector<std::vector<uint16_t>> triJoints(tris);
  for (size_t t = 0; t < tris; ++t) {
    auto collect = [&]() {
      std::set<uint16_t> s;
      for (int k = 0; k < 3; ++k) {
        const SkinInfluences& v = inf[t * 3 + static_cast<size_t>(k)];
        for (uint8_t e = 0; e < v.count; ++e)
          if (v.weight[e] > 0) s.insert(v.joint[e]);
      }
      return s;
    };
    std::set<uint16_t> js = collect();
    bool trimmed = false;
    while (js.size() > maxBones) {
      // The joint whose largest weight in this triangle is smallest loses all of its influences here.
      uint16_t victim = *js.begin();
      float victimMax = 1e30f;
      for (uint16_t j : js) {
        float m = 0;
        for (int k = 0; k < 3; ++k) {
          const SkinInfluences& v = inf[t * 3 + static_cast<size_t>(k)];
          for (uint8_t e = 0; e < v.count; ++e)
            if (v.joint[e] == j) m = std::max(m, v.weight[e]);
        }
        if (m < victimMax) { victimMax = m; victim = j; }
      }
      for (int k = 0; k < 3; ++k) {
        SkinInfluences& v = inf[t * 3 + static_cast<size_t>(k)];
        uint8_t w = 0;
        float keep = 0;
        SkinInfluences nv;
        for (uint8_t e = 0; e < v.count; ++e) {
          if (v.joint[e] == victim) { ++out.droppedInfluences; continue; }
          nv.joint[w] = v.joint[e];
          nv.weight[w] = v.weight[e];
          keep += v.weight[e];
          ++w;
        }
        nv.count = w;
        if (keep > 0)
          for (uint8_t e = 0; e < w; ++e) nv.weight[e] /= keep;
        v = nv;
      }
      trimmed = true;
      js = collect();
    }
    if (trimmed) ++out.trimmedTriangles;
    triJoints[t].assign(js.begin(), js.end());
  }
  if (out.droppedInfluences)
    warning = std::to_string(out.droppedInfluences) + " smallest skin influences dropped on " + std::to_string(out.trimmedTriangles) +
              " triangles that use more than " + std::to_string(maxBones) + " joints; weights renormalized";

  // 2. Assign each triangle to a batch (first fit with the smallest joint growth), joints per batch <= maxBones.
  struct Batch { std::set<uint16_t> joints; std::vector<uint32_t> tris; };
  std::vector<Batch> batches;
  for (size_t t = 0; t < tris; ++t) {
    int best = -1;
    size_t bestGrow = 0;
    for (size_t b = 0; b < batches.size(); ++b) {
      size_t grow = 0;
      for (uint16_t j : triJoints[t]) grow += batches[b].joints.count(j) ? 0 : 1;
      if (batches[b].joints.size() + grow > maxBones) continue;
      if (best < 0 || grow < bestGrow) { best = static_cast<int>(b); bestGrow = grow; }
      if (grow == 0) break;
    }
    if (best < 0) {
      batches.emplace_back();
      best = static_cast<int>(batches.size()) - 1;
    }
    batches[static_cast<size_t>(best)].joints.insert(triJoints[t].begin(), triJoints[t].end());
    batches[static_cast<size_t>(best)].tris.push_back(static_cast<uint32_t>(t));
  }

  // 3. Emit vertices batch by batch with slot-remapped 8-bit weights.
  LitMesh& o = out.mesh;
  const bool hasUv = !in.uvs.empty(), hasCol = !in.colors.empty();
  for (const Batch& b : batches) {
    PackBoneBatch pb = {};
    pb.firstVertex = static_cast<uint32_t>(o.positions.size());
    pb.vertexCount = static_cast<uint32_t>(b.tris.size() * 3);
    memset(pb.bones, kBatchBoneUnused, sizeof pb.bones);
    std::vector<uint16_t> bones(b.joints.begin(), b.joints.end());  // ascending joint index
    for (size_t i = 0; i < bones.size(); ++i) pb.bones[i] = static_cast<uint8_t>(bones[i]);
    pb.boneCount = static_cast<uint32_t>(bones.size());
    for (uint32_t t : b.tris)
      for (int k = 0; k < 3; ++k) {
        const size_t vi = static_cast<size_t>(t) * 3 + static_cast<size_t>(k);
        o.positions.push_back(in.positions[vi]);
        o.normals.push_back(in.normals[vi]);
        if (hasUv) { o.uvs.push_back(in.uvs[vi * 2]); o.uvs.push_back(in.uvs[vi * 2 + 1]); }
        if (hasCol) o.colors.push_back(in.colors[vi]);
        float w[8] = {};
        const SkinInfluences& v = inf[vi];
        bool any = false;
        for (uint8_t e = 0; e < v.count; ++e) {
          const auto it = std::find(bones.begin(), bones.end(), v.joint[e]);
          if (it == bones.end() || v.weight[e] <= 0) continue;
          w[it - bones.begin()] += v.weight[e];
          any = true;
        }
        if (!any) { w[0] = 1.0f; ++out.unweightedVertices; }
        std::array<uint8_t, 8> q;
        quantizeWeights128(w, q.data());
        o.weights.push_back(q);
      }
    out.batches.push_back(pb);
  }
  return true;
}

}  // namespace rensga
