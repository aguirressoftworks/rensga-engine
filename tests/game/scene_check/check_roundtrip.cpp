// F1.05: every registered type (built-in components and the template's scripts) round-trips
// reflection -> JSON (plain + ordered) -> binary blob -> runtime object with identical values.
#include <string.h>

#include "rensga/core/components.h"
#include "rensga/core/reflect_json.h"
#include "rensga/core/world.h"
#include "rensga_test.h"

using namespace rensga;

// Deterministic non-default values for every field type.
static void scramble(const TypeInfo& t, void* obj, int seed) {
  u8* base = static_cast<u8*>(obj);
  for (u32 i = 0; i < t.fieldCount; ++i) {
    const FieldDesc& f = t.fields[i];
    u8* p = base + f.offset;
    float k = static_cast<float>(seed + static_cast<int>(i) + 1);
    switch (f.type) {
      case FieldType::Scalar: *reinterpret_cast<Scalar*>(p) = 0.25f * k + 1.5f; break;
      case FieldType::I32: *reinterpret_cast<i32*>(p) = 3 * (seed + static_cast<int>(i)) - 7; break;
      case FieldType::Bool: *reinterpret_cast<bool*>(p) = ((seed + static_cast<int>(i)) & 1) == 0; break;
      case FieldType::Vector3: *reinterpret_cast<Vector3*>(p) = Vector3(k, -k * 0.5f, k * 0.125f); break;
      case FieldType::Quaternion: *reinterpret_cast<Quaternion*>(p) = Quaternion(0.5f, -0.5f, 0.5f, 0.5f); break;
      case FieldType::Color32:
        *reinterpret_cast<Color32*>(p) = Color32(u8(10 * k), u8(20 + k), u8(255 - k), u8(100 + k));
        break;
      case FieldType::Guid: Guid::parse("00000000-0000-4000-8000-00000000d00d", *reinterpret_cast<Guid*>(p)); break;
    }
  }
}

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
    alignas(16) unsigned char a[512], b[512], c[512], d[512];
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
    u8 blob[512];
    CHECK(blobSize(t) <= sizeof blob);
    writeBlob(t, c, blob);
    CHECK(readBlob(t, d, blob, blobSize(t)));
    CHECK(fieldsEqual(t, a, d));
    // hot-reload state transfer agrees
    u8 st[1024];
    u32 n = saveState(t, a, st, sizeof st);
    CHECK(n > 0);
    alignas(16) unsigned char e[512];
    t.construct(e);
    CHECK_EQ(loadState(t, e, st, n), static_cast<int>(t.fieldCount));
    CHECK(fieldsEqual(t, a, e));
    for (unsigned char* o : {a, b, c, d, e}) t.destruct(o);
    ++checked;
  }
  CHECK(checked >= 10);
}
