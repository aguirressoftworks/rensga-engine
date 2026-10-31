// F6 G1 harness (C0): "every component type is tested through reflection, the inspector, a prefab override and the cooker".
//
//   runComponentMatrix(opts)  walks EVERY type of the TypeRegistry (engine components, project scripts, data types) and runs
//     reflect  scrambled values: fieldsEqual differs from the defaults; saveState / loadState and a copy keep every value
//     json     fieldsToJson / fieldsToOrderedJson -> fieldsFromJson gives the same object (declaration order kept)
//     blob     writeBlob -> readBlob, the trimmed blob (APPENDED_FIELDS) and every older field boundary read back
//     cook     (hook) a scene with that component is cooked for linux and psp, the pack is loaded and the fields compare equal
//     inspector (hook) an inspector edit then undo / redo leaves the component exactly as it was          [E0 fills it]
//     prefab   (hook) a prefab instance override of a scrambled field survives apply / revert / prefab edit [A1 fills it]
//   and prints one line per type:
//     component_matrix: <Type> reflect=ok json=ok blob=ok cook=ok inspector=pending prefab=pending
//
// A hook is a plain function pointer in matrixHooks(); a step with no hook prints "pending" (or "skip" when the type has nothing to
// test there, e.g. cook for a type that is not a Component) and only fails the run when MatrixOptions::require* says it must be
// there (Stage 4 acceptance turns those on). The run also fails when a registered type has no row, so a new type cannot slip past
// the matrix, and when a type in kExpectedComponents is not registered.
//
// Header only; needs the host JSON library (rensga_reflect_json) and nothing else. The cook hook lives in
// tests/cooker/component_matrix_cook.cpp (it needs the cooker library), the inspector hook is installed by the editor test.
#pragma once
#include <stdio.h>
#include <string.h>

#include <functional>
#include <string>
#include <vector>

#include "rensga/core/components.h"
#include "rensga/core/reflect.h"
#include "rensga/core/reflect_json.h"
#include "rensga/core/unity_event.h"

namespace rensga {
namespace matrix {

// One step of the matrix for one type. `scrambled` is an object of the type holding non-default values for every field.
// Return true on success; on failure fill *why (one line, names the field when it can).
using StepFn = std::function<bool(const TypeInfo& type, const void* scrambled, std::string* why)>;

struct Hooks {
  StepFn cook;       // tests/cooker/component_matrix_cook.cpp
  StepFn inspector;  // editor: edit through the inspector model, undo, redo (E0)
  StepFn prefab;     // prefab instance override, apply, revert (A1)
};
inline Hooks& hooks() {
  static Hooks h;
  return h;
}

struct Options {
  bool requireCook = false;       // fail when the cook hook is not installed
  bool requireInspector = false;  // Stage 4 acceptance: true
  bool requirePrefab = false;     // Stage 4 acceptance: true
  bool verbose = true;            // print a line per type
};

enum class State { Ok, Fail, Pending, Skip };
inline const char* stateName(State s) {
  switch (s) {
    case State::Ok: return "ok";
    case State::Fail: return "FAIL";
    case State::Pending: return "pending";
    case State::Skip: return "skip";
  }
  return "?";
}

struct Row {
  std::string type;
  State reflect = State::Pending, json = State::Pending, blob = State::Pending, cook = State::Pending, inspector = State::Pending,
        prefab = State::Pending;
  std::string why;  // first failure
};

// Engine component types that must stay registered (the matrix fails if one disappears).
inline const std::vector<const char*>& expectedComponents() {
  static const std::vector<const char*> v = {
      "MeshRenderer", "BoxCollider", "SphereCollider", "CharacterController", "Camera", "DirectionalLight", "AudioSource", "Light",
      "RenderSettings", "SkinnedMeshRenderer", "Animation", "SpriteRenderer", "SpriteAnimator", "Tilemap", "TilemapCollider2D",
      "BoxCollider2D", "Camera2D", "UIText", "UIImage", "UIButton", "Rigidbody2D",
      // F6 contract (C0)
      "Rigidbody", "CapsuleCollider", "ParticleSystem", "LineRenderer", "Animator", "Canvas", "Slider", "Toggle", "LayoutGroup",
      "EventSystem"};
  return v;
}

// ---- scrambling ------------------------------------------------------------------------------------------------------
// Deterministic non-default values for every field type. `guidFor(fieldName)` supplies the Guid of Guid fields (cook needs
// references that resolve; the default is a fixed non-null Guid).
inline void scramble(const TypeInfo& t, void* obj, int seed, const std::function<Guid(const FieldDesc&)>& guidFor = nullptr) {
  u8* base = static_cast<u8*>(obj);
  for (u32 i = 0; i < t.fieldCount; ++i) {
    const FieldDesc& f = t.fields[i];
    u8* p = base + f.offset;
    float k = static_cast<float>(seed % 7 + static_cast<int>(i) + 1);
    Guid g;
    Guid::parse("00000000-0000-4000-8000-00000000d00d", g);
    if (guidFor && (f.type == FieldType::Guid || (f.type == FieldType::Array && f.elemType == FieldType::Guid))) g = guidFor(f);
    switch (f.type) {
      case FieldType::Scalar: *reinterpret_cast<Scalar*>(p) = 0.25f * k + 1.5f; break;
      case FieldType::I32: *reinterpret_cast<i32*>(p) = 3 * (seed % 11 + static_cast<int>(i)) - 7; break;
      case FieldType::Bool: *reinterpret_cast<bool*>(p) = !*reinterpret_cast<bool*>(p); break;  // always differs from the default
      case FieldType::Vector3: *reinterpret_cast<Vector3*>(p) = Vector3(k, -k * 0.5f, k * 0.125f); break;
      case FieldType::Quaternion: *reinterpret_cast<Quaternion*>(p) = Quaternion(0.5f, -0.5f, 0.5f, 0.5f); break;
      case FieldType::Color32: *reinterpret_cast<Color32*>(p) = Color32(u8(10 * k), u8(20 + k), u8(255 - k), u8(100 + k)); break;
      case FieldType::Guid: *reinterpret_cast<Guid*>(p) = g; break;
      case FieldType::UnityEvent: break;  // covered by the unity_event tests
      case FieldType::String: {
        char text[32];
        snprintf(text, sizeof text, "s%d_%u", seed % 100, i);
        reinterpret_cast<String32*>(p)->Set(text);
        break;
      }
      case FieldType::Enum: {
        const u32 n = enumLabelCount(f);
        i32 cur = *reinterpret_cast<i32*>(p);
        *reinterpret_cast<i32*>(p) = n > 1 ? static_cast<i32>((static_cast<u32>(cur) + 1) % n) : cur + 1;
        break;
      }
      case FieldType::Vector2: *reinterpret_cast<Vector2*>(p) = Vector2(k + 0.5f, -k); break;
      case FieldType::Array: {
        const u32 es = fieldBlobSize(f.elemType);
        const u32 count = f.arrayCap / 2 + 1 > f.arrayCap ? f.arrayCap : f.arrayCap / 2 + 1;
        memset(p, 0, 4 + f.arrayCap * es);
        memcpy(p, &count, 4);
        for (u32 e = 0; e < count; ++e) {
          u8* q = p + 4 + e * es;
          float v = k + static_cast<float>(e);
          switch (f.elemType) {
            case FieldType::I32: *reinterpret_cast<i32*>(q) = static_cast<i32>(v * 3) - 5; break;
            case FieldType::Guid: *reinterpret_cast<Guid*>(q) = g; break;
            case FieldType::Vector3: *reinterpret_cast<Vector3*>(q) = Vector3(v, -v, v * 0.5f); break;
            default: *reinterpret_cast<Scalar*>(q) = 0.5f * v + 0.25f; break;
          }
        }
        break;
      }
    }
  }
}

// RAII object of a reflected type (16-byte aligned, up to 1 KB).
struct Obj {
  const TypeInfo* t;
  alignas(16) unsigned char mem[1024];
  explicit Obj(const TypeInfo& type) : t(&type) {
    memset(mem, 0, sizeof mem);
    if (type.size <= sizeof mem) type.construct(mem);
  }
  ~Obj() {
    if (t->size <= sizeof mem) t->destruct(mem);
  }
  Obj(const Obj&) = delete;
  Obj& operator=(const Obj&) = delete;
  bool fits() const { return t->size <= sizeof mem; }
  void* p() { return mem; }
};

// Names the first field that differs, or "" when equal.
inline std::string firstDifference(const TypeInfo& t, const void* a, const void* b) {
  if (fieldsEqual(t, a, b)) return "";
  const u8* pa = static_cast<const u8*>(a);
  const u8* pb = static_cast<const u8*>(b);
  for (u32 i = 0; i < t.fieldCount; ++i) {
    // compare field by field through the one-field-at-a-time blob so the report names the field
    const FieldDesc& f = t.fields[i];
    TypeInfo one = t;
    FieldDesc fd = f;
    fd.offset = 0;
    one.fields = &fd;
    one.fieldCount = 1;
    one.trimFloor = kNoTrim;
    if (!fieldsEqual(one, pa + f.offset, pb + f.offset)) return f.name;
  }
  return "(unknown field)";
}

// ---- the in-process steps ------------------------------------------------------------------------------------------------
inline bool stepReflect(const TypeInfo& t, const void* scrambled, std::string* why) {
  Obj def(t), copy(t);
  if (t.fieldCount && fieldsEqual(t, scrambled, def.p())) { *why = "scrambled object equals the defaults"; return false; }
  std::vector<u8> st(stateBlobSize(t) + 16);
  u32 n = saveState(t, scrambled, st.data(), static_cast<u32>(st.size()));
  if (!n) { *why = "saveState failed"; return false; }
  int restored = loadState(t, copy.p(), st.data(), n);
  if (restored != static_cast<int>(t.fieldCount)) { *why = "loadState restored " + std::to_string(restored) + " of " + std::to_string(t.fieldCount); return false; }
  std::string d = firstDifference(t, scrambled, copy.p());
  if (!d.empty()) { *why = "state transfer changed field " + d; return false; }
  // a truncated state blob is refused without touching the object
  Obj untouched(t);
  if (n > 1 && loadState(t, untouched.p(), st.data(), n - 1) != -1) { *why = "truncated state blob accepted"; return false; }
  // field paths: every field is addressable by name
  for (u32 i = 0; i < t.fieldCount; ++i) {
    FieldPath fp;
    if (!parseFieldPath(t, t.fields[i].name, fp) || fp.field != &t.fields[i]) { *why = std::string("parseFieldPath fails for ") + t.fields[i].name; return false; }
  }
  return true;
}

inline bool stepJson(const TypeInfo& t, const void* scrambled, std::string* why) {
  Obj a(t), b(t);
  std::string err;
  nlohmann::json j = nlohmann::json::parse(fieldsToJson(t, scrambled).dump());  // through text, as a file would
  if (!fieldsFromJson(t, a.p(), j, &err)) { *why = "fieldsFromJson: " + err; return false; }
  std::string d = firstDifference(t, scrambled, a.p());
  if (!d.empty()) { *why = "json round trip changed field " + d; return false; }
  nlohmann::ordered_json oj = nlohmann::ordered_json::parse(fieldsToOrderedJson(t, scrambled).dump());
  u32 k = 0;
  for (auto it = oj.begin(); it != oj.end(); ++it, ++k)
    if (k >= t.fieldCount || it.key() != t.fields[k].name) { *why = "ordered json is not in declaration order"; return false; }
  if (k != t.fieldCount) { *why = "ordered json lost fields"; return false; }
  if (!fieldsFromJson(t, b.p(), oj, &err)) { *why = "ordered fieldsFromJson: " + err; return false; }
  d = firstDifference(t, scrambled, b.p());
  if (!d.empty()) { *why = "ordered json round trip changed field " + d; return false; }
  // JSON that names an unknown field is refused, naming it
  nlohmann::json bad = nlohmann::json::object();
  bad["__no_such_field__"] = 1;
  if (fieldsFromJson(t, b.p(), bad, &err) || err.find("__no_such_field__") == std::string::npos) { *why = "unknown field not reported"; return false; }
  return true;
}

inline bool stepBlob(const TypeInfo& t, const void* scrambled, std::string* why) {
  const u32 full = blobSize(t);
  std::vector<u8> blob(full ? full : 1);
  writeBlob(t, scrambled, blob.data());
  Obj a(t);
  if (!readBlob(t, a.p(), blob.data(), full)) { *why = "readBlob refused the full blob"; return false; }
  std::string d = firstDifference(t, scrambled, a.p());
  if (!d.empty()) { *why = "blob round trip changed field " + d; return false; }
  // writing twice gives the same bytes (canonical: zero padding after strings and past array counts)
  std::vector<u8> blob2(full ? full : 1);
  writeBlob(t, a.p(), blob2.data());
  if (blob != blob2) { *why = "blob bytes are not canonical"; return false; }
  // a longer blob and a cut inside a field are refused
  std::vector<u8> longer(full + 1, 0);
  memcpy(longer.data(), blob.data(), full);
  Obj junk(t);
  if (readBlob(t, junk.p(), longer.data(), full + 1)) { *why = "blob longer than the type accepted"; return false; }
  // every older layout (blob ends on a field boundary): the missing trailing fields keep the object's own values
  u32 acc = 0;
  for (u32 i = 0; i + 1 < t.fieldCount; ++i) {
    acc += fieldBlobSize(t.fields[i]);
    Obj partial(t), defaults(t);
    if (!readBlob(t, partial.p(), blob.data(), acc)) { *why = "readBlob refused a blob that ends on a field boundary"; return false; }
    for (u32 q = i + 1; q < t.fieldCount; ++q) {
      TypeInfo one = t;
      FieldDesc fd = t.fields[q];
      fd.offset = 0;
      one.fields = &fd;
      one.fieldCount = 1;
      one.trimFloor = kNoTrim;
      const u8* pp = static_cast<const u8*>(partial.p()) + t.fields[q].offset;
      const u8* dp = static_cast<const u8*>(defaults.p()) + t.fields[q].offset;
      if (!fieldsEqual(one, pp, dp)) { *why = std::string("a short blob changed the default of ") + t.fields[q].name; return false; }
    }
  }
  // a blob that ends inside a field is refused
  u32 start = 0;
  for (u32 i = 0; i < t.fieldCount; ++i) {
    const u32 sz = fieldBlobSize(t.fields[i]);
    if (sz > 1) {
      Obj cut(t);
      if (readBlob(t, cut.p(), blob.data(), start + sz / 2)) { *why = std::string("a blob cut inside field ") + t.fields[i].name + " was accepted"; return false; }
    }
    start += sz;
  }
  // the trimmed blob (what the cooker writes for APPENDED_FIELDS types) reads back to the same object
  Obj defaults(t);
  const u32 trimmed = trimmedBlobSize(t, scrambled, defaults.p());
  if (trimmed > full) { *why = "trimmedBlobSize larger than the blob"; return false; }
  Obj tr(t);
  if (!readBlob(t, tr.p(), blob.data(), trimmed)) { *why = "readBlob refused the trimmed blob"; return false; }
  d = firstDifference(t, scrambled, tr.p());
  if (!d.empty()) { *why = "trimmed blob changed field " + d; return false; }
  // and an all-default object trims to its pre-append size
  const u32 defTrim = trimmedBlobSize(t, defaults.p(), defaults.p());
  if (t.trimFloor != kNoTrim) {
    u32 floorBytes = 0;
    for (u32 i = 0; i < t.trimFloor && i < t.fieldCount; ++i) floorBytes += fieldBlobSize(t.fields[i]);
    if (defTrim != floorBytes) { *why = "a default object does not trim to the pre-append size"; return false; }
  } else if (defTrim != full) {
    *why = "a type without APPENDED_FIELDS trims";
    return false;
  }
  return true;
}

// ---- the run -----------------------------------------------------------------------------------------------------------------
inline State runStep(const StepFn& fn, const TypeInfo& t, const void* scrambled, bool applicable, Row& row) {
  if (!applicable) return State::Skip;
  if (!fn) return State::Pending;
  std::string why;
  if (fn(t, scrambled, &why)) return State::Ok;
  if (row.why.empty()) row.why = why;
  return State::Fail;
}

// Runs the matrix over the whole registry. Returns the number of problems (0 = pass); rows gets one entry per registered type.
inline int run(const Options& opt, std::vector<Row>* rowsOut = nullptr) {
  registerBuiltinComponents();
  const TypeRegistry& reg = TypeRegistry::instance();
  std::vector<Row> rows;
  int problems = 0;
  for (u32 id = 0; id < reg.count(); ++id) {
    const TypeInfo& t = *reg.byId(id);
    Row row;
    row.type = t.name;
    Obj sc(t);
    if (!sc.fits()) {
      row.reflect = row.json = row.blob = State::Fail;
      row.why = "type is larger than the 1 KB matrix buffer";
    } else {
      scramble(t, sc.p(), static_cast<int>(id));
      auto inproc = [&](bool (*fn)(const TypeInfo&, const void*, std::string*)) {
        std::string why;
        if (fn(t, sc.p(), &why)) return State::Ok;
        if (row.why.empty()) row.why = why;
        return State::Fail;
      };
      row.reflect = inproc(stepReflect);
      row.json = inproc(stepJson);
      row.blob = inproc(stepBlob);
      const bool isComponent = t.asComponent != nullptr;
      row.cook = runStep(hooks().cook, t, sc.p(), isComponent, row);
      row.inspector = runStep(hooks().inspector, t, sc.p(), isComponent, row);
      row.prefab = runStep(hooks().prefab, t, sc.p(), isComponent, row);
    }
    if (opt.requireCook && row.cook == State::Pending) row.cook = State::Fail;
    if (opt.requireInspector && row.inspector == State::Pending) row.inspector = State::Fail;
    if (opt.requirePrefab && row.prefab == State::Pending) row.prefab = State::Fail;
    const State all[] = {row.reflect, row.json, row.blob, row.cook, row.inspector, row.prefab};
    bool bad = false;
    for (State s : all) bad = bad || s == State::Fail;
    if (bad) {
      ++problems;
      if (row.why.empty()) row.why = "a required step has no hook installed";
    }
    if (opt.verbose) {
      printf("component_matrix: %s reflect=%s json=%s blob=%s cook=%s inspector=%s prefab=%s%s%s\n", row.type.c_str(),
             stateName(row.reflect), stateName(row.json), stateName(row.blob), stateName(row.cook), stateName(row.inspector),
             stateName(row.prefab), bad ? "  <- " : "", bad ? row.why.c_str() : "");
    }
    rows.push_back(row);
  }
  // Coverage: one row per registered type, and every expected engine component is registered.
  if (rows.size() != reg.count()) {
    fprintf(stderr, "component_matrix: %zu rows for %u registered types\n", rows.size(), reg.count());
    ++problems;
  }
  for (const char* n : expectedComponents())
    if (!reg.find(n)) {
      fprintf(stderr, "component_matrix: expected component '%s' is not registered\n", n);
      ++problems;
    }
  for (u32 id = 0; id < reg.count(); ++id) {
    bool found = false;
    for (const Row& r : rows) found = found || r.type == reg.byId(id)->name;
    if (!found) {
      fprintf(stderr, "component_matrix: registered type '%s' is missing from the table\n", reg.byId(id)->name);
      ++problems;
    }
  }
  if (opt.verbose) printf("component_matrix: %u types, %d problem(s)\n", reg.count(), problems);
  if (rowsOut) *rowsOut = rows;
  return problems;
}

}  // namespace matrix
}  // namespace rensga
