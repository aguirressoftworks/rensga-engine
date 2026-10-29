#include <fstream>
#include <set>
#include <string>

#include "rensga/Sim.h"
#include "rensga/core/reflect_json.h"
#include "rensga_test.h"

using namespace rensga;
using nlohmann::json;

TEST(scene_json_parses_and_matches_code_level) {
  std::ifstream f(std::string(RENSGA_PROJECT_DIR_STR) + "/Assets/Scenes/TestLevel.scene");
  CHECK(f.good());
  json scene = json::parse(f);
  CHECK_EQ(scene["format"].get<int>(), 1);
  CHECK(scene["guid"].is_string());

  std::set<std::string> primGuids = {"00000000-0000-4000-8000-00000000c001", "00000000-0000-4000-8000-00000000c002",
                                     "00000000-0000-4000-8000-00000000c003", "00000000-0000-4000-8000-00000000c004",
                                     "00000000-0000-4000-8000-00000000c005"};
  std::set<std::string> guids, names;
  int pickups = 0;
  for (const json& o : scene["objects"]) {
    Guid g;
    CHECK(Guid::parse(o["guid"].get<std::string>().c_str(), g));
    CHECK(guids.insert(o["guid"].get<std::string>()).second);
    names.insert(o["name"].get<std::string>());
    if (!o["parent"].is_null()) CHECK(guids.count(o["parent"].get<std::string>()) == 1);
    for (const json& c : o["components"]) {
      const TypeInfo* t = TypeRegistry::instance().find(c["type"].get<std::string>().c_str());
      CHECK(t != nullptr);
      if (!t) continue;
      alignas(16) unsigned char storage[256];
      CHECK(t->size <= sizeof(storage));
      t->construct(storage);
      std::string err;
      CHECK(fieldsFromJson(*t, storage, c["fields"], &err));
      if (!err.empty()) fprintf(stderr, "  %s: %s\n", t->name, err.c_str());
      t->destruct(storage);
      if (c["type"] == "Pickup") ++pickups;
      if (c["type"] == "MeshRenderer") CHECK(primGuids.count(c["fields"]["mesh"].get<std::string>()) == 1);
    }
  }
  CHECK_EQ(pickups, 5);

  Arena arena;
  arena.create(MemoryConfig::fromRamBudget(24u * 1024 * 1024).arenaBytes);
  World world;
  WorldConfig cfg;
  world.init(cfg, arena);
  CHECK(buildTestLevel(world));
  CHECK_EQ(world.objectCount(), static_cast<u32>(scene["objects"].size()));
  for (u32 i = 0; i < world.objectHighWater(); ++i)
    if (GameObject* go = world.objectAt(i)) CHECK(names.count(go->name) == 1);
  world.shutdown();
}
