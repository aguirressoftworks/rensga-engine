// F6.08 (A2): Physics.json (+ TagManager.json layer names) -> PHY3.
#include "../../tools/cooker/src/physics_cook.h"
#include "rensga_test.h"

using namespace rensga;

TEST(physics_cook_defaults_and_layer_matrix) {
  PackPhysics3D ph;
  std::string err;
  CHECK(cooker::cookPhysics3D("{\"format\":1}", "", ph, err));
  CHECK(ph.gravity[0] == 0.0f && ph.gravity[1] == -9.81f && ph.gravity[2] == 0.0f);
  CHECK((ph.flags & kPhy3QueriesHitTriggers) != 0);
  for (u32 m : ph.masks) CHECK(m == 0xFFFFFFFFu);
  CHECK(ph.bounceThreshold == 2.0f && ph.sleepThreshold == 0.05f);

  const char* tags = "{\"format\":1,\"layers\":[\"Default\",\"TransparentFX\",\"Ignore Raycast\",\"Target\",\"\",\"Player\"]}";
  CHECK(cooker::cookPhysics3D(
      "{\"format\":1,\"gravity\":[0,-20,1],\"ignore\":[[3,5],[\"Player\",\"Ignore Raycast\"],[7,7]],\"queriesHitTriggers\":false,\"sleepThreshold\":0.1}", tags,
      ph, err));
  CHECK(ph.gravity[1] == -20.0f && ph.gravity[2] == 1.0f);
  CHECK((ph.flags & kPhy3QueriesHitTriggers) == 0);
  CHECK((ph.masks[3] & (1u << 5)) == 0 && (ph.masks[5] & (1u << 3)) == 0);   // symmetric
  CHECK((ph.masks[5] & (1u << 2)) == 0 && (ph.masks[2] & (1u << 5)) == 0);   // by name
  CHECK((ph.masks[7] & (1u << 7)) == 0);                                      // a layer ignoring itself
  CHECK((ph.masks[3] & (1u << 3)) != 0 && (ph.masks[0] & 1u) != 0);           // everything else still collides
  CHECK(ph.sleepThreshold == 0.1f);
}

TEST(physics_cook_rejects_bad_input) {
  PackPhysics3D ph;
  std::string err;
  CHECK(!cooker::cookPhysics3D("{\"format\":2}", "", ph, err));
  CHECK(!cooker::cookPhysics3D("{\"format\":1,\"gravity\":[0,-9]}", "", ph, err));
  CHECK(!cooker::cookPhysics3D("{\"format\":1,\"gravity\":[0,-5000,0]}", "", ph, err));
  CHECK(!cooker::cookPhysics3D("{\"format\":1,\"ignore\":[[1,40]]}", "", ph, err));
  CHECK(!cooker::cookPhysics3D("{\"format\":1,\"ignore\":[[\"Nope\",1]]}", "{\"format\":1,\"layers\":[\"Default\"]}", ph, err));
  CHECK(err.find("Nope") != std::string::npos);
}
