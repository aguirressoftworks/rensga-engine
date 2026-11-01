// F6 gate G1 (Stage 4): ONE run of the component matrix with every column filled and required:
//   reflect, json, blob          in tests/fixtures/component_matrix.h
//   cook                         tests/cooker/component_matrix_cook.cpp (hook installed by its static registrar)
//   inspector                    here: one field of the type is edited through the inspector model (editFields, the same call the
//                                Inspector panel makes), undo and redo restore the document JSON exactly
//   prefab                       tests/editor/test_prefab_matrix.cpp (hook installed by its static registrar)
// The run fails when a registered type is missing from the table or any column of a Component is pending or FAIL.
#include <filesystem>
#include <string>

#include "fixtures/component_matrix.h"
#include "rensga/editor/inspector_model.h"
#include "rensga/editor/scene_document.h"
#include "rensga_test.h"

using namespace rensga;
using namespace rensga::editor;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

bool inspectorStep(const TypeInfo& t, const void* scrambled, std::string* why) {
  SceneDocument d;
  std::string err;
  if (!d.load(fs::path(RENSGA_TEMPLATE_DIR) / "Assets/Scenes/TestLevel.scene", err)) { *why = "template scene: " + err; return false; }
  const std::string g = d.createObject("G1", "");
  d.select(g);
  if (!addComponentTo(d, g, t.name)) { *why = "addComponentTo failed"; return false; }
  const SceneObject* o = d.find(g);
  int idx = -1;
  for (size_t i = 0; o && i < o->components.size(); ++i)
    if (o->components[i].type == t.name) idx = static_cast<int>(i);
  if (idx < 0) { *why = "component not on the object after add"; return false; }

  const json def = typeDefaults(t);
  const json scr = fieldsToJson(t, scrambled);
  std::string field;
  for (u32 i = 0; i < t.fieldCount && field.empty(); ++i) {
    const char* n = t.fields[i].name;
    if (t.fields[i].type == FieldType::UnityEvent) continue;
    if (scr.contains(n) && (!def.contains(n) || scr[n] != def[n])) field = n;
  }
  if (field.empty()) { *why = "no field differs between defaults and the scrambled object"; return false; }

  const ComponentRef ref{g, idx};
  const nlohmann::ordered_json before = d.document();
  const size_t depth = d.undoDepth();
  if (!editFields(d, {ref}, field, scr[field])) { *why = "editFields refused " + field; return false; }
  const nlohmann::ordered_json after = d.document();
  if (after == before) { *why = "edit of " + field + " changed nothing"; return false; }
  if (d.undoDepth() != depth + 1) { *why = "edit of " + field + " is not one undo step"; return false; }
  if (!d.undo() || d.document() != before) { *why = "undo not exact after editing " + field; return false; }
  if (!d.redo() || d.document() != after) { *why = "redo not exact after editing " + field; return false; }
  return true;
}

struct Install {
  Install() { rensga::matrix::hooks().inspector = inspectorStep; }
} g_install;

}  // namespace

TEST(component_matrix_g1) {
  matrix::Options opt;
  opt.requireCook = opt.requireInspector = opt.requirePrefab = true;
  std::vector<matrix::Row> rows;
  const int problems = matrix::run(opt, &rows);
  CHECK_EQ(problems, 0);
  CHECK_EQ(rows.size(), TypeRegistry::instance().count());
  size_t components = 0, full = 0, pending = 0;
  for (const matrix::Row& r : rows) {
    const matrix::State all[] = {r.reflect, r.json, r.blob, r.cook, r.inspector, r.prefab};
    bool allOk = true;
    for (matrix::State s : all) {
      pending += s == matrix::State::Pending;
      allOk = allOk && (s == matrix::State::Ok || s == matrix::State::Skip);
    }
    const bool comp = r.inspector != matrix::State::Skip;
    components += comp;
    full += comp && allOk && r.cook == matrix::State::Ok && r.inspector == matrix::State::Ok && r.prefab == matrix::State::Ok;
  }
  printf("component_matrix_g1: %zu registered types, %zu components with all six columns ok, %zu pending\n", rows.size(), full, pending);
  CHECK_EQ(pending, 0u);
  CHECK(components >= 30);
  CHECK_EQ(full, components);
}
