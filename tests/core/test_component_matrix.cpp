// F6 G1 harness skeleton (C0): ctest "component_matrix". Walks every registered type through reflection, JSON, blob and (when the
// cooker hook is installed) the cooker, and lists the inspector and prefab steps as pending until E0 and A1 install their hooks.
// See tests/fixtures/component_matrix.h. This file is compiled into rensga_core_tests (no cook hook: cook=pending, not a failure)
// and into rensga_component_matrix (tests/cooker/CMakeLists.txt, with the cook hook: every Component must cook=ok).
#include "fixtures/component_matrix.h"
#include "rensga_test.h"

using namespace rensga;

TEST(component_matrix) {
  matrix::Options opt;
#ifdef RENSGA_MATRIX_REQUIRE_COOK
  opt.requireCook = true;
#endif
  std::vector<matrix::Row> rows;
  const int problems = matrix::run(opt, &rows);
  CHECK_EQ(problems, 0);
  CHECK(rows.size() >= 30);
  CHECK_EQ(rows.size(), TypeRegistry::instance().count());
}

// The new reflection field types, one by one (the matrix above also covers them through the F6 components).
struct MatrixProbe : Component {
  REFLECT(MatrixProbe)
  SERIALIZE_FIELD(Scalar, speed, 1.5f)
  SERIALIZE_FIELD(String32, label, String32("hello"))
  SERIALIZE_ENUM(mode, 1, "Idle|Walk|Run")
  SERIALIZE_FIELD(Vector2, size, Vector2(2, 3))
  APPENDED_FIELDS()
  SERIALIZE_ARRAY(Vector3, points, 4)
  SERIALIZE_ARRAY(i32, ids, 3)
  SERIALIZE_ARRAY(Guid, refs, 2)
  SERIALIZE_ARRAY(Scalar, times, 5)
};

TEST(reflect_f6_field_types) {
  const TypeInfo& t = *MatrixProbe::staticTypeInfo();
  CHECK_EQ(t.fieldCount, 8u);
  CHECK(t.fields[1].type == FieldType::String && fieldBlobSize(t.fields[1]) == 32);
  CHECK(t.fields[2].type == FieldType::Enum && enumLabelCount(t.fields[2]) == 3);
  char label[16];
  CHECK(enumLabel(t.fields[2], 2, label, sizeof label) && strcmp(label, "Run") == 0);
  CHECK(!enumLabel(t.fields[2], 3, label, sizeof label) && label[0] == 0);
  CHECK_EQ(enumIndexOf(t.fields[2], "Walk"), 1);
  CHECK_EQ(enumIndexOf(t.fields[2], "walk"), -1);
  CHECK(t.fields[3].type == FieldType::Vector2 && fieldBlobSize(t.fields[3]) == 8);
  CHECK(t.fields[4].type == FieldType::Array && t.fields[4].elemType == FieldType::Vector3 && t.fields[4].arrayCap == 4);
  CHECK_EQ(fieldBlobSize(t.fields[4]), 4u + 4u * 12u);
  CHECK_EQ(fieldBlobSize(t.fields[6]), 4u + 2u * 16u);
  CHECK_EQ(t.trimFloor, 4u);
  CHECK_EQ(blobSize(t), 4u + 32u + 4u + 8u + 52u + 16u + 36u + 24u);

  MatrixProbe a, b;
  CHECK_EQ(a.mode, 1);
  CHECK(fieldsEqual(t, &a, &b));
  a.label.Set("a very long label that does not fit in 32 bytes at all");
  CHECK_EQ(strlen(a.label.c_str()), 31u);  // cut, always NUL terminated
  CHECK(!fieldsEqual(t, &a, &b));
  a.mode = 2;
  a.size = Vector2(-1, 7.5f);
  a.points.Add(Vector3(1, 2, 3));
  a.points.Add(Vector3(4, 5, 6));
  a.ids.Add(42);
  Guid g;
  Guid::parse("00000000-0000-4000-8000-00000000abcd", g);
  a.refs.Add(g);
  a.times.Add(0.5f);
  a.times.Add(1.5f);

  // blob: canonical bytes (unused slots zero) and an exact round trip
  u8 blob[256], blob2[256];
  CHECK(blobSize(t) <= sizeof blob);
  memset(blob, 0xAA, sizeof blob);
  writeBlob(t, &a, blob);
  MatrixProbe c;
  CHECK(readBlob(t, &c, blob, blobSize(t)));
  CHECK(fieldsEqual(t, &a, &c));
  CHECK_EQ(c.points.count, 2u);
  CHECK(c.points.items[1] == Vector3(4, 5, 6));
  CHECK(c.refs.items[0] == g && c.refs.count == 1u);
  writeBlob(t, &c, blob2);
  CHECK(memcmp(blob, blob2, blobSize(t)) == 0);
  // stale data in unused slots is not written
  c.points.items[3] = Vector3(9, 9, 9);
  writeBlob(t, &c, blob2);
  CHECK(memcmp(blob, blob2, blobSize(t)) == 0);
  CHECK(fieldsEqual(t, &a, &c));  // and does not count as a difference

  // a count past the capacity in a damaged blob is clamped, not trusted
  u32 big = 99;
  memcpy(blob + 4 + 32 + 4 + 8, &big, 4);
  MatrixProbe d;
  CHECK(readBlob(t, &d, blob, blobSize(t)));
  CHECK_EQ(d.points.count, 4u);

  // old layout: a blob with only the first four fields leaves the appended arrays at their defaults
  MatrixProbe e;
  e.points.Add(Vector3(7, 7, 7));
  CHECK(readBlob(t, &e, blob, 4 + 32 + 4 + 8));
  CHECK_EQ(e.mode, 2);
  CHECK_EQ(e.points.count, 1u);
  CHECK(!readBlob(t, &e, blob, 4 + 32 + 4 + 8 + 10));  // ends inside the points array

  // trim: defaults in the appended fields are left out of the cooked blob, set ones are kept
  MatrixProbe def;
  CHECK_EQ(trimmedBlobSize(t, &def, &def), 4u + 32u + 4u + 8u);
  CHECK_EQ(trimmedBlobSize(t, &a, &def), blobSize(t));
  MatrixProbe onlyIds;
  onlyIds.ids.Add(1);
  CHECK_EQ(trimmedBlobSize(t, &onlyIds, &def), 4u + 32u + 4u + 8u + 52u + 16u);  // refs and times are default, ids is not

  // JSON: string, enum label or number, vector2, arrays
  nlohmann::json j = fieldsToJson(t, &a);
  CHECK(j["label"].is_string() && j["mode"] == 2 && j["size"].size() == 2 && j["points"].size() == 2);
  CHECK(j["refs"][0] == "00000000-0000-4000-8000-00000000abcd");
  MatrixProbe f;
  std::string err;
  CHECK(fieldsFromJson(t, &f, j, &err));
  CHECK(fieldsEqual(t, &a, &f));
  CHECK(fieldsFromJson(t, &f, nlohmann::json::parse("{\"mode\":\"Run\"}"), &err) && f.mode == 2);
  CHECK(fieldsFromJson(t, &f, nlohmann::json::parse("{\"mode\":0}"), &err) && f.mode == 0);
  CHECK(!fieldsFromJson(t, &f, nlohmann::json::parse("{\"mode\":\"Sprint\"}"), &err) && err.find("mode") != std::string::npos);
  CHECK(!fieldsFromJson(t, &f, nlohmann::json::parse("{\"ids\":[1,2,3,4]}"), &err) && err.find("ids") != std::string::npos);  // over capacity
  CHECK(!fieldsFromJson(t, &f, nlohmann::json::parse("{\"ids\":[1,\"x\"]}"), &err));
  CHECK(!fieldsFromJson(t, &f, nlohmann::json::parse("{\"label\":\"0123456789012345678901234567890123456789\"}"), &err));
  CHECK(!fieldsFromJson(t, &f, nlohmann::json::parse("{\"size\":[1]}"), &err));
  CHECK(fieldsFromJson(t, &f, nlohmann::json::parse("{\"ids\":[]}"), &err) && f.ids.count == 0);

  // state transfer keeps arrays and survives a recompile that changed the capacity (copies what fits)
  u8 st[512];
  u32 n = saveState(t, &a, st, sizeof st);
  CHECK_EQ(n, stateBlobSize(t));
  MatrixProbe h;
  CHECK_EQ(loadState(t, &h, st, n), 8);
  CHECK(fieldsEqual(t, &a, &h));
  CHECK_EQ(loadState(t, &h, st, n - 1), -1);

  // field paths into arrays
  FieldPath p;
  CHECK(parseFieldPath(t, "points.1", p) && p.field == &t.fields[4] && p.index == 1 && p.part == -1);
  CHECK(parseFieldPath(t, "points.3.z", p) && p.index == 3 && p.part == 2);
  CHECK(!parseFieldPath(t, "points.4", p));      // past the capacity
  CHECK(!parseFieldPath(t, "ids.0.x", p));       // i32 elements have no parts
  CHECK(!parseFieldPath(t, "points.x", p));
  CHECK(parseFieldPath(t, "size.y", p) && p.part == 1);
  CHECK(!parseFieldPath(t, "label.x", p));
  CHECK(!parseFieldPath(t, "mode.x", p));
}
