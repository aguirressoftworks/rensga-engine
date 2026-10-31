// F6 G1 harness (C0): the cook step of the component matrix (tests/fixtures/component_matrix.h). For one component type it writes a
// temporary project whose only object carries that component with scrambled values, cooks it for linux and for the psp profile,
// loads the pack and compares the cooked component with the values that went in. Installed by a static registrar, so linking this
// file into a test executable is all it takes.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

#include "fixtures/component_matrix.h"
#include "rensga/core/pack_format.h"
#include "rensga/cooker/cooker.h"

using namespace rensga;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const char* kObjGuid = "00000000-0000-4000-8000-0000000000a1";
const char* kSceneGuid = "00000000-0000-4000-8000-00000000d0f1";

// Guid fields that name an object of the scene (everything else that is a Guid names an asset, and the cook step has none).
bool isObjectRef(const char* type, const char* field) {
  static const std::set<std::string> refs = {
      "Camera2D.target",     "Slider.fillRect",       "Slider.handleRect",      "Toggle.graphic",
      "EventSystem.firstSelected", "UIButton.selectOnUp", "UIButton.selectOnDown", "UIButton.selectOnLeft", "UIButton.selectOnRight"};
  return refs.count(std::string(type) + "." + field) != 0;
}

// Asset references the cooker resolves: the glTF character, its material and a primitive of samples/psp3d_room.
const char* assetGuid(const char* type, const char* field) {
  const std::string k = std::string(type) + "." + field;
  if (k == "MeshRenderer.mesh") return "00000000-0000-4000-8000-00000000c001";
  if (k == "MeshRenderer.material" || k == "SkinnedMeshRenderer.material") return "00000000-0000-4000-8000-000000000505";
  if (k == "SkinnedMeshRenderer.mesh" || k == "Animation.clip") return "00000000-0000-4000-8000-000000000501";
  return nullptr;
}

// Copies the assets assetGuid() names (and what they depend on) into the temporary project.
void copyRoomAssets(const fs::path& dir) {
  const fs::path room = fs::path(RENSGA_ROOM_DIR) / "Assets";
  for (const char* rel : {"Primitives/cube.prim.json", "Models/blocky_walk.gltf", "Textures/blocky_walk.png", "Textures/room.png",
                          "Materials/character.mat.json"}) {
    fs::create_directories((dir / "Assets" / rel).parent_path());
    fs::copy_file(room / rel, dir / "Assets" / rel, fs::copy_options::overwrite_existing);
    fs::copy_file(room / (std::string(rel) + ".meta"), dir / "Assets" / (std::string(rel) + ".meta"), fs::copy_options::overwrite_existing);
  }
}

bool writeText(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p);
  out << text;
  return static_cast<bool>(out);
}

bool cookStep(const TypeInfo& t, const void* /*scrambled*/, std::string* why) {
  const fs::path dir = fs::temp_directory_path() / ("rensga_matrix_cook_" + std::string(t.name));
  fs::remove_all(dir);
  fs::create_directories(dir);
  fs::copy(fs::path(RENSGA_FIXTURES_DIR) / "cook_ok" / "ProjectSettings", dir / "ProjectSettings", fs::copy_options::recursive);
  if (strcmp(t.name, "MeshRenderer") == 0 || strcmp(t.name, "SkinnedMeshRenderer") == 0 || strcmp(t.name, "Animation") == 0) copyRoomAssets(dir);

  // The component under test plus the ones it requires, all allowed by the ConsoleTarget.
  json comps = json::array();
  json ct;
  {
    std::ifstream in(dir / "ProjectSettings/ConsoleTarget.json");
    ct = json::parse(in);
  }
  auto allow = [&](const char* name) {
    for (const auto& n : ct["allowedComponents"])
      if (n == name) return;
    ct["allowedComponents"].push_back(name);
  };
  allow(t.name);
  for (u32 i = 0; i < t.requireCount; ++i) {
    const TypeInfo* rt = TypeRegistry::instance().find(t.requireNames[i]);
    if (!rt) continue;
    allow(rt->name);
    comps.push_back({{"type", rt->name}, {"fields", json::object()}});
  }
  if (strcmp(t.name, "Animation") == 0) {  // a skeletal clip is only cooked for a glTF that a SkinnedMeshRenderer uses
    allow("SkinnedMeshRenderer");
    comps.push_back({{"type", "SkinnedMeshRenderer"},
                     {"fields", {{"mesh", assetGuid("SkinnedMeshRenderer", "mesh")}, {"material", assetGuid("SkinnedMeshRenderer", "material")}}}});
  }
  ct["features"] = json::array({"lighting", "skinning", "fog"});
  {
    std::ofstream out(dir / "ProjectSettings/ConsoleTarget.json");
    out << ct.dump(1);
  }

  // Scrambled values; object references point at the scene's own object so the cooker's GUID check has something to find.
  matrix::Obj exp(t);
  matrix::scramble(t, exp.p(), 3, [&](const FieldDesc& f) {
    Guid g;
    if (isObjectRef(t.name, f.name)) Guid::parse(kObjGuid, g);
    else if (const char* a = assetGuid(t.name, f.name)) Guid::parse(a, g);
    return g;
  });
  comps.push_back({{"type", t.name}, {"fields", fieldsToJson(t, exp.p())}});

  json scene = {{"format", 1}, {"guid", kSceneGuid}, {"objects", json::array()}};
  scene["objects"].push_back({{"guid", kObjGuid}, {"name", "Subject"}, {"parent", nullptr}, {"active", true},
                              {"transform", {{"position", {1, 2, 3}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}},
                              {"components", comps}, {"prefab", nullptr}});
  if (!writeText(dir / "Assets/Scenes/matrix.scene", scene.dump(1)) ||
      !writeText(dir / "Assets/Scenes/matrix.scene.meta", std::string("{\"guid\":\"") + kSceneGuid + "\",\"importSettings\":{}}")) {
    *why = "cannot write the temporary project";
    return false;
  }

  for (const char* target : {"linux", "psp"}) {
    CookOptions o;
    o.project = dir;
    o.target = target;
    o.force = true;
    std::string firstError;
    CookResult r = cookProject(o, [&](const CookMessage& m) {
      if (m.severity == CookMessage::Error && firstError.empty()) firstError = m.text;
    });
    if (!r.ok) {
      *why = std::string("cook for ") + target + " failed: " + firstError;
      return false;
    }
    std::ifstream in(r.packPath, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    u8* mem = static_cast<u8*>(aligned_alloc(16, (bytes.size() + 15) & ~size_t(15)));
    memcpy(mem, bytes.data(), bytes.size());
    PackView view;
    bool ok = view.open(mem, bytes.size(), "matrix");
    bool found = false;
    std::string problem;
    if (ok) {
      u32 n = 0;
      const PackComponent* pc = view.records<PackComponent>(pack_section::COMP, &n);
      const u8* fdat = view.section(pack_section::FDAT);
      for (u32 i = 0; i < n && !found; ++i) {
        if (pc[i].typeHash != t.hash) continue;
        found = true;
        matrix::Obj got(t);
        if (!readBlob(t, got.p(), fdat + pc[i].fieldOffset, pc[i].fieldSize)) {
          problem = "the cooked blob does not read back";
        } else {
          std::string d = matrix::firstDifference(t, exp.p(), got.p());
          if (!d.empty()) problem = "cooked field " + d + " differs";
        }
        if (problem.empty() && t.trimFloor == kNoTrim && pc[i].fieldSize != blobSize(t)) problem = "a type without APPENDED_FIELDS was trimmed";
      }
    }
    free(mem);
    if (!ok) { *why = std::string("the ") + target + " pack does not open"; return false; }
    if (!found) { *why = std::string("the ") + target + " pack has no " + t.name + " component"; return false; }
    if (!problem.empty()) { *why = std::string(target) + ": " + problem; return false; }
  }
  fs::remove_all(dir);
  return true;
}

struct Install {
  Install() { rensga::matrix::hooks().cook = cookStep; }
} g_install;

}  // namespace
