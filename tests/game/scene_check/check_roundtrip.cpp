// F1.05: every registered type (built-in components and the template's scripts) round-trips
// reflection -> JSON (plain + ordered) -> binary blob -> runtime object with identical values.
#include <string.h>

#include "rensga/core/components.h"
#include "rensga/core/reflect_json.h"
#include "rensga/core/world.h"
#include "fixtures/component_matrix.h"
#include "rensga_test.h"

using namespace rensga;

// Deterministic non-default values for every field type (F6: tests/fixtures/component_matrix.h knows all of them).
static void scramble(const TypeInfo& t, void* obj, int seed) { matrix::scramble(t, obj, seed); }

TEST(reflect_all_types_roundtrip) {
  const TypeRegistry& reg = TypeRegistry::instance();
  registerBuiltinComponents();
  static const char* kBuiltins[] = {"MeshRenderer", "BoxCollider", "SphereCollider", "CharacterController", "Camera",
                                    "DirectionalLight"};
  static const char* kScripts[] = {"PlayerController", "FollowCamera", "Pickup", "PickupCounter"};
  for (const char* n : kBuiltins) CHECK(reg.find(n) != nullptr);
  for (const char* n : kScripts) CHECK(reg.find(n) != nullptr);
  u32 checked = 0;
  for (u32 id = 0; id < reg.count(); ++id) {
    const TypeInfo& t = *reg.byId(id);
    CHECK(t.fieldCount <= kMaxFields);
    alignas(16) unsigned char a[1024], b[1024], c[1024], d[1024];
    CHECK(t.size <= sizeof a);
    t.construct(a);
    t.construct(b);
    t.construct(c);
    t.construct(d);
    scramble(t, a, static_cast<int>(id));
    if (t.fieldCount) CHECK(!fieldsEqual(t, a, b));  // scrambled differs from defaults

    // reflection -> JSON (sorted) -> object
    std::string err;
    CHECK(fieldsFromJson(t, b, nlohmann::json::parse(fieldsToJson(t, a).dump()), &err));
    CHECK(fieldsEqual(t, a, b));
    // reflection -> ordered JSON (text) -> object, key order = declaration order
    nlohmann::ordered_json oj = nlohmann::ordered_json::parse(fieldsToOrderedJson(t, a).dump());
    u32 k = 0;
    for (auto it = oj.begin(); it != oj.end(); ++it, ++k) CHECK(it.key() == t.fields[k].name);
    CHECK_EQ(k, t.fieldCount);
    CHECK(fieldsFromJson(t, c, oj, &err));
    CHECK(fieldsEqual(t, a, c));
    // JSON-built object -> binary blob -> runtime object
    u8 blob[1024];
    CHECK(blobSize(t) <= sizeof blob);
    writeBlob(t, c, blob);
    CHECK(readBlob(t, d, blob, blobSize(t)));
    CHECK(fieldsEqual(t, a, d));
    // hot-reload state transfer agrees
    u8 st[2048];
    u32 n = saveState(t, a, st, sizeof st);
    CHECK(n > 0);
    alignas(16) unsigned char e[1024];
    t.construct(e);
    CHECK_EQ(loadState(t, e, st, n), static_cast<int>(t.fieldCount));
    CHECK(fieldsEqual(t, a, e));
    for (unsigned char* o : {a, b, c, d, e}) t.destruct(o);
    ++checked;
  }
  CHECK(checked >= 10);
}
