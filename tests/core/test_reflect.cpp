#include <string.h>

#include "rensga/core/reflect.h"
#include "rensga/core/reflect_json.h"
#include "rensga_test.h"

using namespace rensga;

struct ReflectProbe {
  REFLECT(ReflectProbe)
  REQUIRE_COMPONENT(Camera)
  HEADER("Movement")
  TOOLTIP("Units per second")
  RANGE(0, 20)
  SERIALIZE_FIELD(Scalar, speed, 5)
  SERIALIZE_FIELD(i32, lives, 3)
  SERIALIZE_FIELD(bool, flag, true)
  SERIALIZE_FIELD(Vector3, offset, Vector3(1, 2, 3))
  SERIALIZE_FIELD(Quaternion, spin, Quaternion(0, 0, 0, 1))
  SERIALIZE_FIELD(Color32, tint, Color32(10, 20, 30, 255))
  SERIALIZE_FIELD(Guid, target, Guid())
};

TEST(reflect_registration) {
  const TypeInfo* t = TypeRegistry::instance().find("ReflectProbe");
  CHECK(t != nullptr);
  CHECK(t == ReflectProbe::staticTypeInfo());
  CHECK(t == TypeRegistry::instance().findByHash(fnv1a32("ReflectProbe")));
  CHECK_EQ(t->hash, fnv1a32("ReflectProbe"));
  CHECK_EQ(t->fieldCount, 7u);
  CHECK(strcmp(t->fields[0].name, "speed") == 0);
  CHECK(t->fields[0].type == FieldType::Scalar);
  CHECK(t->fields[0].hasRange && t->fields[0].rangeMax == 20.0f);
  CHECK(t->fields[0].header && strcmp(t->fields[0].header, "Movement") == 0);
  CHECK(t->fields[0].tooltip && strcmp(t->fields[0].tooltip, "Units per second") == 0);
  CHECK_EQ(t->requireCount, 1u);
  CHECK(strcmp(t->requireNames[0], "Camera") == 0);
  CHECK(!t->fields[1].hasRange && t->fields[1].header == nullptr);  // modifiers do not leak
  CHECK_EQ(t->fields[0].offset, static_cast<u32>(offsetof(ReflectProbe, speed)));
  CHECK_EQ(t->fields[3].offset, static_cast<u32>(offsetof(ReflectProbe, offset)));
  CHECK(t->fields[6].type == FieldType::Guid);
  alignas(16) unsigned char mem[sizeof(ReflectProbe)];
  t->construct(mem);  // factory builds defaults
  CHECK_EQ(reinterpret_cast<ReflectProbe*>(mem)->speed, 5.0f);
  CHECK(reinterpret_cast<ReflectProbe*>(mem)->offset == Vector3(1, 2, 3));
}

static void mutate(ReflectProbe& p) {
  p.speed = 12.5f;
  p.lives = -4;
  p.flag = false;
  p.offset = Vector3(-1, 0.25f, 9);
  p.spin = Quaternion(0.5f, 0.5f, 0.5f, 0.5f);
  p.tint = Color32(255, 128, 0, 77);
  Guid::parse("00000000-0000-4000-8000-00000000c003", p.target);
}

TEST(reflect_binary_roundtrip) {
  const TypeInfo& t = *ReflectProbe::staticTypeInfo();
  ReflectProbe a, b;
  mutate(a);
  CHECK(!fieldsEqual(t, &a, &b));
  CHECK_EQ(blobSize(t), 4u + 4 + 1 + 12 + 16 + 4 + 16);
  u8 blob[64];
  writeBlob(t, &a, blob);
  CHECK(!readBlob(t, &b, blob, blobSize(t) - 1));  // wrong size refused
  CHECK(readBlob(t, &b, blob, blobSize(t)));
  CHECK(fieldsEqual(t, &a, &b));
  CHECK(b.tint == Color32(255, 128, 0, 77));
}

TEST(reflect_json_roundtrip) {
  const TypeInfo& t = *ReflectProbe::staticTypeInfo();
  ReflectProbe a, b;
  mutate(a);
  nlohmann::json j = fieldsToJson(t, &a);
  CHECK(j["target"] == "00000000-0000-4000-8000-00000000c003");
  CHECK(j["tint"].size() == 4);
  std::string text = j.dump();
  nlohmann::json parsed = nlohmann::json::parse(text);
  std::string err;
  CHECK(fieldsFromJson(t, &b, parsed, &err));
  CHECK(fieldsEqual(t, &a, &b));
}

TEST(reflect_json_partial_and_errors) {
  const TypeInfo& t = *ReflectProbe::staticTypeInfo();
  ReflectProbe p;
  std::string err;
  CHECK(fieldsFromJson(t, &p, nlohmann::json::parse(R"({"speed":7,"tint":[1.0,0.0,0.0,1.0]})"), &err));
  CHECK_EQ(p.speed, 7.0f);
  CHECK_EQ(p.lives, 3);                       // untouched
  CHECK(p.tint == Color32(255, 0, 0, 255));  // float colors are 0..1
  CHECK(!fieldsFromJson(t, &p, nlohmann::json::parse(R"({"nope":1})"), &err));
  CHECK(err.find("nope") != std::string::npos);
  CHECK(!fieldsFromJson(t, &p, nlohmann::json::parse(R"({"offset":[1,2]})"), &err));
  CHECK(err.find("offset") != std::string::npos);
}

TEST(reflect_ordered_json_keeps_declaration_order) {
  const TypeInfo& t = *ReflectProbe::staticTypeInfo();
  ReflectProbe a, b;
  mutate(a);
  nlohmann::ordered_json j = fieldsToOrderedJson(t, &a);
  std::string keys;
  for (auto it = j.begin(); it != j.end(); ++it) keys += it.key() + ",";
  CHECK(keys == "speed,lives,flag,offset,spin,tint,target,");
  std::string err;
  CHECK(fieldsFromJson(t, &b, j, &err));
  CHECK(fieldsEqual(t, &a, &b));
}

TEST(reflect_state_transfer_by_name) {
  const TypeInfo& t = *ReflectProbe::staticTypeInfo();
  ReflectProbe a, b;
  mutate(a);
  u8 buf[256];
  CHECK_EQ(saveState(t, &a, buf, 8), 0u);  // too small
  u32 n = saveState(t, &a, buf, sizeof buf);
  CHECK_EQ(n, stateBlobSize(t));
  CHECK_EQ(loadState(t, &b, buf, n), 7);
  CHECK(fieldsEqual(t, &a, &b));
  CHECK_EQ(loadState(t, &b, buf, n - 1), -1);  // malformed: untouched
  // A "recompiled" type: same blob, one field renamed (hash differs) -> the others still restore.
  u32 oldHash = fnv1a32("lives");
  u32 renamed = fnv1a32("livesLeft");
  ReflectProbe c;
  c.lives = 77;
  for (u32 off = 4; off + 5 <= n;) {
    u32 h;
    memcpy(&h, buf + off, 4);
    if (h == oldHash) memcpy(buf + off, &renamed, 4);
    off += 5 + fieldBlobSize(static_cast<FieldType>(buf[off + 4]));
  }
  CHECK_EQ(loadState(t, &c, buf, n), 6);
  CHECK_EQ(c.lives, 77);
  CHECK_EQ(c.speed, 12.5f);
}

TEST(reflect_field_paths) {
  const TypeInfo& t = *ReflectProbe::staticTypeInfo();
  FieldPath p;
  CHECK(parseFieldPath(t, "speed", p));
  CHECK(p.field == &t.fields[0] && p.part == -1);
  CHECK(parseFieldPath(t, "offset.z", p) && p.part == 2);
  CHECK(parseFieldPath(t, "spin.w", p) && p.part == 3);
  CHECK(parseFieldPath(t, "tint.a", p) && p.part == 3);
  CHECK(!parseFieldPath(t, "offset.w", p));
  CHECK(!parseFieldPath(t, "speed.x", p));
  CHECK(!parseFieldPath(t, "nope", p));
  CHECK(!parseFieldPath(t, "", p));
}
