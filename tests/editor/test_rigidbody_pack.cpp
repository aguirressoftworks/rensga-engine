// F4.08 part 1: a Rigidbody2D scene goes through the real cooker (ConsoleTarget check, reflection layout), the pack loader and the
// GameLoop. The tilemap is written by TilemapEdit, so this also proves the editor's output cooks.
#include <unistd.h>

#include <filesystem>
#include <fstream>

#include "rensga/Rensga.h"
#include "rensga/cooker/cooker.h"
#include "rensga/editor/tilemap_edit.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

void writeText(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream(p, std::ios::binary) << text;
}

json objectJson(const char* guid, const char* name, double x, double y, json components) {
  return json{{"guid", guid},
              {"name", name},
              {"parent", nullptr},
              {"active", true},
              {"transform", {{"position", {x, y, 0}}, {"rotation", {0, 0, 0, 1}}, {"scale", {1, 1, 1}}}},
              {"components", components},
              {"prefab", nullptr}};
}

constexpr const char* kTileset = "00000000-0000-4000-8000-000000000101";
constexpr const char* kMap = "00000000-0000-4000-8000-0000000004a1";
constexpr const char* kScene = "00000000-0000-4000-8000-0000000004b1";

// Builds a tiny project: a floor made with TilemapEdit (tile 0 is solid) and a scene with one body of each type.
fs::path makeProject(const char* tag) {
  const fs::path root = fs::temp_directory_path() / (std::string("rensga_rb_") + tag + "_" + std::to_string(static_cast<long>(getpid())));
  fs::remove_all(root);
  const fs::path sample(RENSGA_SAMPLE_DIR);
  fs::create_directories(root / "Assets/Textures");
  fs::copy_file(sample / "Assets/Textures/tiles.png", root / "Assets/Textures/tiles.png");
  fs::copy_file(sample / "Assets/Textures/tiles.png.meta", root / "Assets/Textures/tiles.png.meta");
  fs::create_directories(root / "ProjectSettings");
  for (const char* f : {"Physics2D.json", "InputActions.json"}) fs::copy_file(sample / "ProjectSettings" / f, root / "ProjectSettings" / f);
  // The sample's ConsoleTarget, limited to what this scene uses.
  std::ifstream in(sample / "ProjectSettings/ConsoleTarget.json");
  json ct = json::parse(in);
  ct["allowedComponents"] = json::array({"Tilemap", "TilemapCollider2D", "BoxCollider2D", "Rigidbody2D"});
  writeText(root / "ProjectSettings/ConsoleTarget.json", ct.dump(1) + "\n");
  writeText(root / "ProjectSettings/Project.json",
            json{{"format", 1}, {"name", "RbTest"}, {"target", "ps2"}, {"scenes", json::array({kScene})}}.dump(1) + "\n");

  editor::TilemapEdit map;
  map.create(kTileset, 32, 20, 10);
  map.paintRect(0, 8, 19, 9, 0);  // floor, surface at y = 256
  map.setTileProps(0, {editor::TileKind::Solid, ""});
  std::string err;
  fs::create_directories(root / "Assets/Tiles");
  CHECK(map.saveAs(root / "Assets/Tiles/floor.tilemap.json", err));
  writeText(root / "Assets/Tiles/floor.tilemap.json.meta", json{{"guid", kMap}, {"importSettings", json::object()}}.dump(1) + "\n");

  auto body = [](const char* guid, const char* name, double x, int type, double vx, double vy) {
    return objectJson(guid, name, x, 20, json::array({json{{"type", "BoxCollider2D"}, {"fields", {{"sizeX", 12}, {"sizeY", 14}}}},
                                                       json{{"type", "Rigidbody2D"},
                                                            {"fields", {{"bodyType", type}, {"gravityScale", 1.0}, {"velocityX", vx}, {"velocityY", vy}}}}}));
  };
  json objects = json::array();
  objects.push_back(objectJson("00000000-0000-4000-8000-0000000004c1", "Map", 0, 0,
                               json::array({json{{"type", "Tilemap"}, {"fields", {{"map", kMap}}}},
                                            json{{"type", "TilemapCollider2D"}, {"fields", json::object()}}})));
  objects.push_back(body("00000000-0000-4000-8000-0000000004c2", "Dyn", 100, 0, 0, 0));
  objects.push_back(body("00000000-0000-4000-8000-0000000004c3", "Kin", 200, 1, 30, 0));
  objects.push_back(body("00000000-0000-4000-8000-0000000004c4", "Sta", 300, 2, 0, 0));
  writeText(root / "Assets/Scenes/Phys.scene", json{{"format", 1}, {"guid", kScene}, {"objects", objects}}.dump(1) + "\n");
  writeText(root / "Assets/Scenes/Phys.scene.meta", json{{"guid", kScene}, {"importSettings", json::object()}}.dump(1) + "\n");
  return root;
}

}  // namespace

TEST(rigidbody2d_scene_cooks_loads_and_runs) {
  const fs::path root = makeProject("run");
  CookOptions o;
  o.project = root.string();
  o.target = "ps2";
  o.force = true;
  std::string errors;
  CookResult r = cookProject(o, [&](const CookMessage& m) {
    if (m.severity == CookMessage::Error) errors += m.text + "\n";
  });
  if (!r.ok) fprintf(stderr, "  cook failed:\n%s", errors.c_str());
  CHECK(r.ok);
  if (!r.ok) return;

  Arena arena;
  StdioFileSystem fsys("", nullptr, (root.string() + "/").c_str());
  CHECK(arena.create(8u * 1024 * 1024));
  Span<const u8> bytes = fsys.readFile(r.packPath.c_str(), arena);
  PackView view;
  CHECK(bytes && view.open(bytes.data, bytes.size, r.packPath.c_str()));
  World world;
  WorldConfig cfg;
  CHECK(world.init(cfg, arena));
  LoadedPack loaded;
  CHECK(world.loadPack(view, &loaded, 0));
  u32 handles[4] = {};
  GfxAssets::bind(&view, &loaded.assets, handles, loaded.assets.textureCount < 4 ? loaded.assets.textureCount : 4);
  Gameplay2D::resetTriggerMemory();
  GameLoop loop;
  loop.init(world);

  GameObject* dyn = GameObject::Find("Dyn");
  GameObject* kin = GameObject::Find("Kin");
  GameObject* sta = GameObject::Find("Sta");
  CHECK(dyn && kin && sta);
  if (dyn && kin && sta) {
    Rigidbody2D* kr = kin->GetComponent<Rigidbody2D>();
    CHECK(kr != nullptr);
    CHECK_EQ(kr->bodyType, RigidbodyType::Kinematic);  // the field values came through the pack
    CHECK_EQ(kr->velocityX, 30.0f);
    VirtualGamepad pad;
    for (int i = 0; i < 180; ++i) CHECK(loop.frame(1.0f / 60.0f, pad));  // false = a heap allocation in a frame
    BoxCollider2D* db = dyn->GetComponent<BoxCollider2D>();
    CHECK_NEAR(db->bounds().y + db->bounds().height, 256.0f, 0.01f);  // stands on the floor made by TilemapEdit
    CHECK(db->grounded);
    CHECK_EQ(dyn->GetComponent<Rigidbody2D>()->velocityY, 0.0f);
    CHECK_NEAR(kin->transform.position().x, 200.0f + 30.0f * 3.0f, 1.0f);  // 3 s at 30 px/s
    CHECK_EQ(kin->transform.position().y, 20.0f);
    CHECK_EQ(sta->transform.position().y, 20.0f);
  }
  world.shutdown();
  GfxAssets::unbind();
  fs::remove_all(root);
}
