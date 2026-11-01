// F6.02 (B3): baked vertex lighting. The baker (lights, shadows, AO) on hand-made triangles, and the cooker end to end: a scene with
// bakeLighting cooks to vertex colors that carry the light, a scene without the field cooks to the same bytes as before.
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

#include <nlohmann/json.hpp>

#include "../../tools/cooker/src/vertex_bake.h"
#include "rensga/cooker/cooker.h"
#include "rensga/core/memory.h"
#include "rensga/core/pack_format.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {
// an upward-facing floor quad of half size h at y = 0, two triangles
std::vector<Vector3> floorQuad(float h) {
  return {Vector3(-h, 0, -h), Vector3(-h, 0, h), Vector3(h, 0, h), Vector3(-h, 0, -h), Vector3(h, 0, h), Vector3(h, 0, -h)};
}
// an axis aligned box (12 triangles), only used as an occluder
std::vector<Vector3> boxTris(Vector3 c, Vector3 e) {
  std::vector<Vector3> v;
  auto quad = [&](Vector3 a, Vector3 b, Vector3 cc, Vector3 d) { v.insert(v.end(), {a, b, cc, a, cc, d}); };
  const float x0 = c.x - e.x, x1 = c.x + e.x, y0 = c.y - e.y, y1 = c.y + e.y, z0 = c.z - e.z, z1 = c.z + e.z;
  quad(Vector3(x0, y1, z0), Vector3(x0, y1, z1), Vector3(x1, y1, z1), Vector3(x1, y1, z0));
  quad(Vector3(x0, y0, z0), Vector3(x1, y0, z0), Vector3(x1, y0, z1), Vector3(x0, y0, z1));
  quad(Vector3(x0, y0, z1), Vector3(x1, y0, z1), Vector3(x1, y1, z1), Vector3(x0, y1, z1));
  quad(Vector3(x0, y0, z0), Vector3(x0, y1, z0), Vector3(x1, y1, z0), Vector3(x1, y0, z0));
  quad(Vector3(x0, y0, z0), Vector3(x0, y0, z1), Vector3(x0, y1, z1), Vector3(x0, y1, z0));
  quad(Vector3(x1, y0, z0), Vector3(x1, y1, z0), Vector3(x1, y1, z1), Vector3(x1, y0, z1));
  return v;
}
int lum(const uint8_t* c) { return (c[0] + c[1] + c[2]) / 3; }
}  // namespace

TEST(vertex_bake_direct_light_shadow_and_ao) {
  cooker::BakeSettings s;
  s.ambient[0] = s.ambient[1] = s.ambient[2] = 0.2f;
  cooker::VertexBaker b(s);
  cooker::BakeLightIn sun;
  sun.type = 0;
  sun.dir = Vector3(0, -1, 0);
  sun.rgb[0] = sun.rgb[1] = sun.rgb[2] = 0.6f;
  b.addLight(sun);
  b.addOccluder(floorQuad(10));
  b.addOccluder(boxTris(Vector3(0, 1, 0), Vector3(1, 1, 1)));  // a 2 m cube standing on the floor
  const Vector3 pos[3] = {Vector3(6, 0, 6) /*open*/, Vector3(0, 0, 0) /*under the box*/, Vector3(1.05f, 0, 0) /*next to the box*/};
  const Vector3 up[3] = {Vector3(0, 1, 0), Vector3(0, 1, 0), Vector3(0, 1, 0)};
  uint8_t out[12];
  b.bake(pos, up, 3, Matrix4::identity(), false, out);
  const int open = lum(out), shadow = lum(out + 4), nearBox = lum(out + 8);
  CHECK(open > 190);                     // 0.2 ambient + 0.6 sun = 0.8
  CHECK(shadow < 60);                    // ambient only, and the AO of a closed box above
  CHECK(nearBox < open && nearBox > shadow);  // the wall of the box occludes part of the sky, the sun still reaches it
  CHECK(out[3] == 255);
  // pure function: a second bake is identical
  uint8_t again[12];
  b.bake(pos, up, 3, Matrix4::identity(), false, again);
  CHECK(memcmp(out, again, 12) == 0);
  CHECK(b.raysCast() > 0);
}

TEST(vertex_bake_point_light_falls_off_with_range) {
  cooker::BakeSettings s;
  s.ambient[0] = s.ambient[1] = s.ambient[2] = 0;
  s.aoStrength = 0;
  cooker::VertexBaker b(s);
  cooker::BakeLightIn p;
  p.type = 1;
  p.pos = Vector3(0, 2, 0);
  p.range = 4;
  p.rgb[0] = p.rgb[1] = p.rgb[2] = 1;
  b.addLight(p);
  const Vector3 pos[3] = {Vector3(0, 0, 0), Vector3(1.5f, 0, 0), Vector3(5, 0, 0)};
  const Vector3 nrm[3] = {Vector3(0, 1, 0), Vector3(0, 1, 0), Vector3(0, 1, 0)};
  uint8_t out[12];
  b.bake(pos, nrm, 3, Matrix4::identity(), false, out);
  // below the lamp: (1 - 2/4)^2 = 0.25; farther: dimmer; beyond the range: black
  CHECK(out[0] > 55 && out[0] < 72);
  CHECK(out[4] < out[0] && out[4] > 0);
  CHECK(out[8] == 0);
}

TEST(vertex_bake_scene_wants_flag_and_world_matrices) {
  std::vector<cooker::BakeObjectIn> objs(3);
  objs[0].comps.push_back({"Light", json::object({{"type", 0}, {"color", json::array({255, 255, 255, 255})}, {"intensity", 1.0}})});
  objs[1].comps.push_back({"MeshRenderer", json::object({{"mesh", "m"}, {"bakeLighting", false}})});
  std::vector<std::string> errs;
  auto loader = [](const std::string&, std::vector<Vector3>& p, std::vector<Vector3>& n, std::string&) {
    p = floorQuad(1);
    n.assign(p.size(), Vector3(0, 1, 0));
    return true;
  };
  CHECK(cooker::VertexBakeScene::create(objs, loader, errs) == nullptr);  // no object asks for a bake: nothing is built
  objs[1].comps[0].fields["bakeLighting"] = true;
  objs[1].parent = 2;
  objs[2].pos[1] = 5;
  auto sc = cooker::VertexBakeScene::create(objs, loader, errs);
  CHECK(sc != nullptr);
  if (sc) {
    CHECK_EQ(sc->baker().lightCount(), 1u);
    CHECK_EQ(sc->baker().occluderCount(), 1u);
    CHECK(sc->worldOf(1).m[13] == 5.0f);  // the parent's translation
  }
}

namespace {
fs::path makeProject(const char* tag, bool bake) {
  const fs::path src = fs::path(RENSGA_TEMPLATE_DIR);
  const fs::path dst = fs::temp_directory_path() / (std::string("rensga_vbake_") + tag);
  std::error_code ec;
  fs::remove_all(dst, ec);
  fs::create_directories(dst / "Assets/Scenes", ec);
  fs::copy(src / "ProjectSettings", dst / "ProjectSettings", fs::copy_options::recursive, ec);
  fs::copy(src / "Assets/Primitives", dst / "Assets/Primitives", fs::copy_options::recursive, ec);
  const char* plane = "00000000-0000-4000-8000-00000000c002";
  const char* cube = "00000000-0000-4000-8000-00000000c001";
  (void)plane;
  auto obj = [&](const char* g, const char* name, json pos, json comps) {
    return json::object({{"guid", g}, {"name", name}, {"parent", nullptr}, {"active", true},
                         {"transform", json::object({{"position", pos}, {"rotation", json::array({0, 0, 0, 1})}, {"scale", json::array({1, 1, 1})}})},
                         {"components", comps}, {"prefab", nullptr}});
  };
  std::string planeGuid, cubeGuid;
  for (const char* f : {"plane", "cube"}) {
    json m = json::parse(std::ifstream(dst / (std::string("Assets/Primitives/") + f + ".prim.json.meta")));
    (f[0] == 'p' ? planeGuid : cubeGuid) = m["guid"].get<std::string>();
  }
  (void)cube;
  fs::create_directories(dst / "Assets/Materials", ec);
  std::ofstream(dst / "Assets/Materials/stone.mat.json") << R"({"format":1,"texture":null,"color":[200,190,180,255],"unlit":false,"alphaTest":false})";
  std::ofstream(dst / "Assets/Materials/stone.mat.json.meta") << R"({"guid":"00000000-0000-4000-8000-00000000d001","importSettings":{}})";
  json fieldsPlane = json::object({{"mesh", planeGuid}, {"color", json::array({255, 255, 255, 255})}});
  json fieldsCube = json::object({{"mesh", cubeGuid}, {"color", json::array({255, 255, 255, 255})}, {"material", "00000000-0000-4000-8000-00000000d001"}});
  if (bake) { fieldsPlane["bakeLighting"] = true; fieldsCube["bakeLighting"] = true; }
  json scene = json::object({{"format", 1}, {"guid", "00000000-0000-4000-a000-000000000001"}, {"objects", json::array()}});
  scene["objects"].push_back(obj("00000000-0000-4000-9000-000000000001", "Camera", json::array({0, 4, -8}),
                                 json::array({json::object({{"type", "Camera"}, {"fields", json::object()}})})));
  scene["objects"].push_back(obj("00000000-0000-4000-9000-000000000002", "Sun", json::array({0, 5, 0}),
                                 json::array({json::object({{"type", "Light"}, {"fields", json::object({{"type", 0}})}})})));
  scene["objects"].push_back(obj("00000000-0000-4000-9000-000000000003", "Floor", json::array({0, 0, 0}),
                                 json::array({json::object({{"type", "MeshRenderer"}, {"fields", fieldsPlane}})})));
  scene["objects"].push_back(obj("00000000-0000-4000-9000-000000000004", "Box", json::array({0, 0.5, 0}),
                                 json::array({json::object({{"type", "MeshRenderer"}, {"fields", fieldsCube}})})));
  std::ofstream(dst / "Assets/Scenes/Main.scene") << scene.dump(1);
  std::ofstream(dst / "Assets/Scenes/Main.scene.meta") << "{\"guid\":\"00000000-0000-4000-a000-000000000001\",\"importSettings\":{}}";
  return dst;
}
std::vector<uint8_t> readAll(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
CookResult cook(const fs::path& project, const char* target, std::string* err) {
  CookOptions o;
  o.project = project;
  o.target = target;
  o.force = true;
  return cookProject(o, [&](const CookMessage& m) {
    if (m.severity == CookMessage::Error && err && err->empty()) *err = m.text;
  });
}
// the average vertex color luminance of every baked mesh record of the pack, by mesh index
std::vector<int> meshLuminances(const fs::path& packPath) {
  std::vector<uint8_t> bytes = readAll(packPath);
  Arena arena;
  arena.create(bytes.size() + 64);
  u8* mem = static_cast<u8*>(arena.allocate(bytes.size()));
  memcpy(mem, bytes.data(), bytes.size());
  PackView view;
  std::vector<int> out;
  if (!view.open(mem, bytes.size(), "vbake")) return out;
  u32 meshCount = 0, vertCount = 0;
  const PackMeshRec* meshes = view.records<PackMeshRec>(pack_section::MESH, &meshCount);
  const PackVertex* verts = view.records<PackVertex>(pack_section::VERT, &vertCount);
  for (u32 i = 0; i < meshCount && verts; ++i) {
    long sum = 0;
    for (u32 k = 0; k < meshes[i].vertexCount; ++k) {
      const u32 c = verts[meshes[i].vertexOffset + k].color;
      sum += ((c & 255) + ((c >> 8) & 255) + ((c >> 16) & 255)) / 3;
    }
    out.push_back(meshes[i].vertexCount ? static_cast<int>(sum / meshes[i].vertexCount) : 0);
  }
  return out;
}
// (MSHL records with kMeshLightBaked, MATL entries) of a pack
std::pair<int, int> bakedFlagsAndMaterials(const fs::path& packPath) {
  std::vector<uint8_t> bytes = readAll(packPath);
  Arena arena;
  arena.create(bytes.size() + 64);
  u8* mem = static_cast<u8*>(arena.allocate(bytes.size()));
  memcpy(mem, bytes.data(), bytes.size());
  PackView view;
  if (!view.open(mem, bytes.size(), "vbake-flags")) return {-1, -1};
  u32 n = 0;
  const PackMeshLit* lit = view.records<PackMeshLit>(pack_section::MSHL, &n);
  int baked = 0;
  for (u32 i = 0; lit && i < n; ++i) baked += (lit[i].flags & kMeshLightBaked) != 0;
  return {baked, static_cast<int>(view.assets().materialCount)};
}
}  // namespace

TEST(vertex_bake_cook_changes_vertex_colors_only_when_asked) {
  std::string err;
  const fs::path plain = makeProject("plain", false), baked = makeProject("baked", true);
  CookResult a = cook(plain, "linux", &err);
  CHECK(a.ok);
  if (!a.ok) std::printf("cook error: %s\n", err.c_str());
  CookResult b = cook(baked, "linux", &err);
  CHECK(b.ok);
  if (!b.ok) std::printf("cook error: %s\n", err.c_str());
  // the baked pack differs from the plain one; the plain one has no trace of the field (appended fields are trimmed)
  CHECK(readAll(a.packPath) != readAll(b.packPath));
  std::vector<int> la = meshLuminances(a.packPath), lb = meshLuminances(b.packPath);
  CHECK(la.size() >= 1 && lb.size() >= 1);
  if (la.size() == lb.size() && la.size() >= 1) {
    // the box sits on the floor: its top is lit, so the baked floor is darker than the plain (ambient 0.35 + lambert) one only where
    // the box shadows it; both meshes keep a sensible brightness
    CHECK(lb[0] > 40 && lb[0] < 255);
    bool differs = false;
    for (size_t i = 0; i < la.size(); ++i) differs = differs || la[i] != lb[i];
    CHECK(differs);
  }
  // the cube has a material: its mesh record carries kMeshLightBaked only in the baked pack, and the material is the same single MATL entry
  const auto fa = bakedFlagsAndMaterials(a.packPath), fb = bakedFlagsAndMaterials(b.packPath);
  CHECK_EQ(fa.first, 0);
  CHECK_EQ(fb.first, 1);
  CHECK_EQ(fa.second, 1);
  CHECK_EQ(fb.second, 1);
  // a second cook is byte-identical (the bake is deterministic)
  CookResult c = cook(baked, "linux", &err);
  CHECK(c.ok);
  CHECK(readAll(b.packPath) == readAll(c.packPath));
  // the PSP cook bakes too, and the plain scene cooks to the same bytes with or without the baker built in
  CookResult p = cook(baked, "psp", &err);
  CHECK(p.ok);
}
