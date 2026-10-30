#include "rensga/editor/scene_document.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

namespace rensga {
namespace editor {

using nlohmann::json;
using nlohmann::ordered_json;
namespace fs = std::filesystem;

std::string generateGuid() {
  static std::mt19937_64 rng{std::random_device{}()};
  uint64_t a = rng(), b = rng();
  a = (a & 0xFFFFFFFFFFFF0FFFull) | 0x0000000000004000ull;  // version 4
  b = (b & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;  // variant 10xx
  char buf[40];
  std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx", static_cast<unsigned>(a >> 32),
                static_cast<unsigned>((a >> 16) & 0xFFFF), static_cast<unsigned>(a & 0xFFFF),
                static_cast<unsigned>(b >> 48), static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFull));
  return buf;
}

namespace {

ordered_json toOrdered(const json& j) { return ordered_json::parse(j.dump()); }

// Round to the shortest text a float round-trips through, so saved files stay clean (0.1 not 0.10000000149).
double quant(float v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.7g", static_cast<double>(v));
  return std::strtod(buf, nullptr);
}

ordered_json arrN(const float* v, size_t n) {
  ordered_json a = ordered_json::array();
  for (size_t i = 0; i < n; ++i) a.push_back(quant(v[i]));
  return a;
}

// ---- tiny affine math (column-major 4x4, m[col*4+row]) ----
struct Mat { double m[16]; };

Mat identity() { Mat r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }

Mat mul(const Mat& a, const Mat& b) {
  Mat r{};
  for (int c = 0; c < 4; ++c)
    for (int rr = 0; rr < 4; ++rr) {
      double s = 0;
      for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
      r.m[c * 4 + rr] = s;
    }
  return r;
}

Mat compose(const float p[3], const float q[4], const float s[3]) {
  double x = q[0], y = q[1], z = q[2], w = q[3];
  double n = std::sqrt(x * x + y * y + z * z + w * w);
  if (n > 0) { x /= n; y /= n; z /= n; w /= n; } else { x = y = z = 0; w = 1; }
  Mat r = identity();
  r.m[0] = (1 - 2 * (y * y + z * z)) * s[0]; r.m[1] = (2 * (x * y + z * w)) * s[0]; r.m[2] = (2 * (x * z - y * w)) * s[0];
  r.m[4] = (2 * (x * y - z * w)) * s[1]; r.m[5] = (1 - 2 * (x * x + z * z)) * s[1]; r.m[6] = (2 * (y * z + x * w)) * s[1];
  r.m[8] = (2 * (x * z + y * w)) * s[2]; r.m[9] = (2 * (y * z - x * w)) * s[2]; r.m[10] = (1 - 2 * (x * x + y * y)) * s[2];
  r.m[12] = p[0]; r.m[13] = p[1]; r.m[14] = p[2];
  return r;
}

Mat invertAffine(const Mat& a) {
  const double* m = a.m;
  // 3x3 block rows: [m0 m4 m8; m1 m5 m9; m2 m6 m10]
  double a00 = m[0], a01 = m[4], a02 = m[8], a10 = m[1], a11 = m[5], a12 = m[9], a20 = m[2], a21 = m[6], a22 = m[10];
  double c00 = a11 * a22 - a12 * a21, c01 = a12 * a20 - a10 * a22, c02 = a10 * a21 - a11 * a20;
  double det = a00 * c00 + a01 * c01 + a02 * c02;
  if (std::fabs(det) < 1e-12) return identity();
  double id = 1 / det;
  double i00 = c00 * id, i01 = (a02 * a21 - a01 * a22) * id, i02 = (a01 * a12 - a02 * a11) * id;
  double i10 = c01 * id, i11 = (a00 * a22 - a02 * a20) * id, i12 = (a02 * a10 - a00 * a12) * id;
  double i20 = c02 * id, i21 = (a01 * a20 - a00 * a21) * id, i22 = (a00 * a11 - a01 * a10) * id;
  Mat r = identity();
  r.m[0] = i00; r.m[4] = i01; r.m[8] = i02;
  r.m[1] = i10; r.m[5] = i11; r.m[9] = i12;
  r.m[2] = i20; r.m[6] = i21; r.m[10] = i22;
  r.m[12] = -(i00 * m[12] + i01 * m[13] + i02 * m[14]);
  r.m[13] = -(i10 * m[12] + i11 * m[13] + i12 * m[14]);
  r.m[14] = -(i20 * m[12] + i21 * m[13] + i22 * m[14]);
  return r;
}

void decompose(const Mat& a, float p[3], float q[4], float s[3]) {
  const double* m = a.m;
  p[0] = static_cast<float>(m[12]); p[1] = static_cast<float>(m[13]); p[2] = static_cast<float>(m[14]);
  double sx = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
  double sy = std::sqrt(m[4] * m[4] + m[5] * m[5] + m[6] * m[6]);
  double sz = std::sqrt(m[8] * m[8] + m[9] * m[9] + m[10] * m[10]);
  double det = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
  if (det < 0) sx = -sx;
  s[0] = static_cast<float>(sx); s[1] = static_cast<float>(sy); s[2] = static_cast<float>(sz);
  if (sx == 0 || sy == 0 || sz == 0) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
  double r00 = m[0] / sx, r10 = m[1] / sx, r20 = m[2] / sx;
  double r01 = m[4] / sy, r11 = m[5] / sy, r21 = m[6] / sy;
  double r02 = m[8] / sz, r12 = m[9] / sz, r22 = m[10] / sz;
  double tr = r00 + r11 + r22, x, y, z, w;
  if (tr > 0) {
    double t = std::sqrt(tr + 1) * 2;
    w = 0.25 * t; x = (r21 - r12) / t; y = (r02 - r20) / t; z = (r10 - r01) / t;
  } else if (r00 > r11 && r00 > r22) {
    double t = std::sqrt(1 + r00 - r11 - r22) * 2;
    w = (r21 - r12) / t; x = 0.25 * t; y = (r01 + r10) / t; z = (r02 + r20) / t;
  } else if (r11 > r22) {
    double t = std::sqrt(1 + r11 - r00 - r22) * 2;
    w = (r02 - r20) / t; x = (r01 + r10) / t; y = 0.25 * t; z = (r12 + r21) / t;
  } else {
    double t = std::sqrt(1 + r22 - r00 - r11) * 2;
    w = (r10 - r01) / t; x = (r02 + r20) / t; y = (r12 + r21) / t; z = 0.25 * t;
  }
  q[0] = static_cast<float>(x); q[1] = static_cast<float>(y); q[2] = static_cast<float>(z); q[3] = static_cast<float>(w);
}

void remapStrings(ordered_json& j, const std::map<std::string, std::string>& m) {
  if (j.is_string()) {
    auto it = m.find(j.get_ref<const std::string&>());
    if (it != m.end()) j = it->second;
  } else if (j.is_array() || j.is_object()) {
    for (auto& v : j) remapStrings(v, m);
  }
}

}  // namespace

bool SceneDocument::load(const fs::path& file, std::string& err) {
  std::ifstream f(file, std::ios::binary);
  if (!f) { err = "cannot open " + file.string(); return false; }
  ordered_json j = ordered_json::parse(f, nullptr, false);
  if (j.is_discarded() || !j.is_object()) { err = "invalid scene JSON in " + file.string(); return false; }
  doc_ = std::move(j);
  path_ = file;
  stack_.clear();
  hasBefore_ = false;
  loaded_ = true;
  changed();
  forcedDirty_ = false;
  stack_.markClean();
  return true;
}

bool SceneDocument::restore(const fs::path& file, ordered_json j, bool dirty) {
  if (!j.is_object()) return false;
  doc_ = std::move(j);
  path_ = file;
  stack_.clear();
  hasBefore_ = false;
  loaded_ = true;
  changed();
  forcedDirty_ = dirty;
  stack_.markClean();
  return true;
}

bool SceneDocument::save(std::string& err) {
  if (!loaded_) { err = "no scene loaded"; return false; }
  std::ofstream f(path_, std::ios::binary | std::ios::trunc);
  if (!f) { err = "cannot write " + path_.string(); return false; }
  f << doc_.dump(1, ' ', false) << "\n";
  f.flush();
  if (!f) { err = "write failed for " + path_.string(); return false; }
  forcedDirty_ = false;
  stack_.markClean();
  return true;
}

const SceneObject* SceneDocument::find(const std::string& guid) const {
  int i = indexOf(guid);
  return i < 0 ? nullptr : &live().objects[i];
}

int SceneDocument::indexOf(const std::string& guid) const {
  if (guid.empty()) return -1;
  for (size_t i = 0; i < live().objects.size(); ++i)
    if (live().objects[i].guid == guid) return static_cast<int>(i);
  return -1;
}

std::vector<std::string> SceneDocument::childrenOf(const std::string& parentGuid) const {
  std::vector<std::string> r;
  for (const SceneObject& o : live().objects) {
    bool root = o.parentGuid.empty() || indexOf(o.parentGuid) < 0;  // orphans show as roots
    if (parentGuid.empty() ? root : o.parentGuid == parentGuid) r.push_back(o.guid);
  }
  return r;
}

bool SceneDocument::isDescendant(const std::string& guid, const std::string& ancestor) const {
  std::string cur = guid;
  for (size_t guard = 0; guard <= live().objects.size(); ++guard) {
    const SceneObject* o = find(cur);
    if (!o || o->parentGuid.empty()) return false;
    if (o->parentGuid == ancestor) return true;
    cur = o->parentGuid;
  }
  return false;
}

void SceneDocument::select(const std::string& guid) {
  selected_ = indexOf(guid) >= 0 ? guid : std::string();
  extraSel_.clear();
}

void SceneDocument::toggleSelect(const std::string& guid) {
  if (indexOf(guid) < 0) return;
  if (selected_.empty()) { selected_ = guid; return; }
  if (guid == selected_) {  // the next one becomes the primary
    if (extraSel_.empty()) selected_.clear();
    else { selected_ = extraSel_.front(); extraSel_.erase(extraSel_.begin()); }
    return;
  }
  auto it = std::find(extraSel_.begin(), extraSel_.end(), guid);
  if (it != extraSel_.end()) extraSel_.erase(it);
  else extraSel_.push_back(guid);
}

std::vector<std::string> SceneDocument::selection() const {
  std::vector<std::string> v;
  if (!selected_.empty()) v.push_back(selected_);
  v.insert(v.end(), extraSel_.begin(), extraSel_.end());
  return v;
}

bool SceneDocument::isSelected(const std::string& guid) const {
  return guid == selected_ || std::find(extraSel_.begin(), extraSel_.end(), guid) != extraSel_.end();
}

void SceneDocument::setSelection(const std::vector<std::string>& guids) {
  std::vector<std::string> ok;
  for (const std::string& g : guids)
    if (indexOf(g) >= 0 && std::find(ok.begin(), ok.end(), g) == ok.end()) ok.push_back(g);
  extraSel_.clear();
  if (ok.empty()) { selected_.clear(); return; }
  selected_ = ok.back();
  ok.pop_back();
  extraSel_ = ok;
}

void SceneDocument::addToSelection(const std::string& guid) {
  if (indexOf(guid) < 0 || guid == selected_) return;
  extraSel_.erase(std::remove(extraSel_.begin(), extraSel_.end(), guid), extraSel_.end());
  if (!selected_.empty()) extraSel_.push_back(selected_);
  selected_ = guid;
}

std::vector<std::string> SceneDocument::topLevel(const std::vector<std::string>& guids) const {
  std::vector<std::string> out;
  for (const SceneObject& o : scene().objects) {
    if (std::find(guids.begin(), guids.end(), o.guid) == guids.end()) continue;
    bool covered = false;
    for (const std::string& g : guids)
      if (g != o.guid && isDescendant(o.guid, g)) { covered = true; break; }
    if (!covered) out.push_back(o.guid);
  }
  return out;
}

std::vector<std::string> SceneDocument::displayRange(const std::string& a, const std::string& b) const {
  std::vector<std::string> order;
  auto walk = [&](auto&& self, const std::string& parent) -> void {
    for (const std::string& k : childrenOf(parent)) {
      order.push_back(k);
      self(self, k);
    }
  };
  walk(walk, "");
  auto ia = std::find(order.begin(), order.end(), a), ib = std::find(order.begin(), order.end(), b);
  if (ia == order.end() || ib == order.end()) return {};
  if (ia > ib) std::swap(ia, ib);
  return std::vector<std::string>(ia, ib + 1);
}

size_t SceneDocument::jsonIndex(const std::string& guid) const {
  if (!doc_.contains("objects")) return static_cast<size_t>(-1);
  const ordered_json& objs = doc_["objects"];
  for (size_t i = 0; i < objs.size(); ++i)
    if (objs[i].value("guid", "") == guid) return i;
  return static_cast<size_t>(-1);
}

ordered_json* SceneDocument::findJson(const std::string& guid) {
  size_t i = jsonIndex(guid);
  return i == static_cast<size_t>(-1) ? nullptr : &doc_["objects"][i];
}

void SceneDocument::collectSubtree(const std::string& guid, std::vector<std::string>& out) const {
  out.push_back(guid);
  for (const SceneObject& o : live().objects)
    if (o.parentGuid == guid && std::find(out.begin(), out.end(), o.guid) == out.end()) collectSubtree(o.guid, out);
}

// One undo step = the document before and after an edit. Consecutive edits with the same key (a drag) merge;
// inside a group (gesture) every snapshot merges, so a whole gesture keeps only its first "before" and last "after".
class SnapshotCommand : public Command {
 public:
  SnapshotCommand(SceneDocument* d, ordered_json before, ordered_json after, std::string key)
      : doc_(d), before_(std::move(before)), after_(std::move(after)), key_(std::move(key)) {}
  const char* name() const override { return key_.empty() ? "Edit scene" : "Edit scene (merged)"; }
  void undo() override { doc_->applySnapshot(before_); }
  void redo() override { doc_->applySnapshot(after_); }
  const std::string& mergeKey() const override { return key_; }
  bool groupMerge() const override { return true; }
  bool mergeWith(const Command& next) override {
    after_ = static_cast<const SnapshotCommand&>(next).after_;
    return true;
  }

 private:
  SceneDocument* doc_;
  ordered_json before_, after_;
  std::string key_;
};

void SceneDocument::applySnapshot(const ordered_json& j) {
  doc_ = j;
  changed();
}

void SceneDocument::pushUndo(const std::string& mergeKey) {
  before_ = doc_;
  beforeKey_ = mergeKey;
  hasBefore_ = true;
}

void SceneDocument::changed() {
  view_ = parseScene(json::parse(doc_.dump()));
  view_.path = path_.string();
  if (!selected_.empty() && indexOf(selected_) < 0) selected_.clear();
  extraSel_.erase(std::remove_if(extraSel_.begin(), extraSel_.end(), [&](const std::string& g) { return indexOf(g) < 0; }), extraSel_.end());
  if (selected_.empty() && !extraSel_.empty()) { selected_ = extraSel_.front(); extraSel_.erase(extraSel_.begin()); }
  if (hasBefore_) {
    hasBefore_ = false;
    stack_.record(std::make_unique<SnapshotCommand>(this, std::move(before_), doc_, beforeKey_));
  }
}

void SceneDocument::beginGesture() { if (!mirror_) stack_.beginGroup("Edit scene"); }
void SceneDocument::endGesture() { if (!mirror_) stack_.endGroup(); }

bool SceneDocument::undo() { return !mirror_ && stack_.undo(); }
bool SceneDocument::redo() { return !mirror_ && stack_.redo(); }

bool SceneDocument::setTransform(const std::string& guid, const float pos[3], const float rot[4], const float scale[3]) {
  if (mirror_) return sink_ && sink_->transform(guid, pos, rot, scale);  // F3.04: play mode edits the running World
  ordered_json* o = findJson(guid);
  if (!o) return false;
  ordered_json np = arrN(pos, 3);
  ordered_json nr = arrN(rot, 4);
  ordered_json ns = arrN(scale, 3);
  const ordered_json* t = o->contains("transform") ? &(*o)["transform"] : nullptr;
  if (t && t->value("position", ordered_json()) == np && t->value("rotation", ordered_json()) == nr &&
      t->value("scale", ordered_json()) == ns)
    return true;  // nothing changed
  pushUndo("xform:" + guid);
  ordered_json& tr = (*o)["transform"];
  if (!tr.is_object()) tr = ordered_json::object();
  tr["position"] = np;
  tr["rotation"] = nr;
  tr["scale"] = ns;
  changed();
  return true;
}

bool SceneDocument::setWorldMatrix(const std::string& guid, const double world[16]) {
  if (mirror_) return false;  // F3.04: the document is frozen while playing
  const SceneObject* o = find(guid);
  if (!o) return false;
  Mat w{}, parentWorld = identity();
  for (int i = 0; i < 16; ++i) w.m[i] = world[i];
  if (!o->parentGuid.empty()) worldMatrix(o->parentGuid, parentWorld.m);
  float p[3], q[4], s[3];
  decompose(mul(invertAffine(parentWorld), w), p, q, s);
  // keep stored values where the matrix round trip only added float noise (small diffs, no spurious edits)
  auto keep = [](float* n, const float* old, int cnt) {
    bool same = true;
    for (int i = 0; i < cnt; ++i) same = same && std::fabs(n[i] - old[i]) < 1e-4f;
    if (same) for (int i = 0; i < cnt; ++i) n[i] = old[i];
  };
  keep(p, o->position, 3); keep(q, o->rotation, 4); keep(s, o->scale, 3);
  return setTransform(guid, p, q, s);
}

bool SceneDocument::setActive(const std::string& guid, bool active) {
  if (mirror_) return sink_ && sink_->active(guid, active);
  ordered_json* o = findJson(guid);
  if (!o) return false;
  if (o->value("active", true) == active) return true;
  pushUndo();
  (*o)["active"] = active;
  changed();
  return true;
}

bool SceneDocument::setTag(const std::string& guid, const std::string& tag) {
  if (mirror_) return false;  // F3.04: the document is frozen while playing
  ordered_json* o = findJson(guid);
  if (!o) return false;
  if (o->value("tag", "") == tag) return true;
  pushUndo();
  if (tag.empty()) o->erase("tag"); else (*o)["tag"] = tag;
  changed();
  return true;
}

bool SceneDocument::setLayer(const std::string& guid, int layer) {
  if (mirror_) return false;  // F3.04: the document is frozen while playing
  ordered_json* o = findJson(guid);
  if (!o) return false;
  if (o->value("layer", 0) == layer) return true;
  pushUndo();
  if (layer == 0) o->erase("layer"); else (*o)["layer"] = layer;
  changed();
  return true;
}

bool SceneDocument::setField(const std::string& guid, int component, const std::string& field, const json& value) {
  if (mirror_) return sink_ && sink_->field(guid, component, field, value);
  ordered_json* o = findJson(guid);
  if (!o || !o->contains("components") || component < 0 || component >= static_cast<int>((*o)["components"].size()))
    return false;
  ordered_json nv = toOrdered(value);
  ordered_json& c = (*o)["components"][component];
  if (c.contains("fields") && c["fields"].contains(field) && c["fields"][field] == nv) return true;
  pushUndo("field:" + guid + ":" + std::to_string(component) + ":" + field);
  if (!c.contains("fields") || !c["fields"].is_object()) c["fields"] = ordered_json::object();
  c["fields"][field] = std::move(nv);
  changed();
  return true;
}

bool SceneDocument::addComponent(const std::string& guid, const std::string& type, const json& fields) {
  if (mirror_) return false;  // F3.04: the document is frozen while playing
  ordered_json* o = findJson(guid);
  if (!o || type.empty()) return false;
  pushUndo();
  ordered_json c = ordered_json::object();
  c["type"] = type;
  c["fields"] = toOrdered(fields);
  if (!o->contains("components") || !(*o)["components"].is_array()) (*o)["components"] = ordered_json::array();
  (*o)["components"].push_back(std::move(c));
  changed();
  return true;
}

bool SceneDocument::removeComponent(const std::string& guid, int component) {
  if (mirror_) return false;  // F3.04: the document is frozen while playing
  ordered_json* o = findJson(guid);
  if (!o || !o->contains("components") || component < 0 || component >= static_cast<int>((*o)["components"].size()))
    return false;
  pushUndo();
  (*o)["components"].erase(static_cast<size_t>(component));
  changed();
  return true;
}

bool SceneDocument::moveComponent(const std::string& guid, int from, int to) {
  if (mirror_) return false;  // F3.04: the document is frozen while playing
  ordered_json* o = findJson(guid);
  if (!o || !o->contains("components")) return false;
  const int n = static_cast<int>((*o)["components"].size());
  if (from < 0 || from >= n || to < 0 || to >= n) return false;
  if (from == to) return true;
  pushUndo();
  ordered_json& comps = (*o)["components"];
  ordered_json c = std::move(comps[from]);
  comps.erase(static_cast<size_t>(from));
  comps.insert(comps.begin() + to, std::move(c));
  changed();
  return true;
}

bool SceneDocument::setComponentFields(const std::string& guid, int component, const json& fields) {
  if (mirror_) return false;  // F3.04: the document is frozen while playing
  ordered_json* o = findJson(guid);
  if (!o || !o->contains("components") || component < 0 || component >= static_cast<int>((*o)["components"].size()))
    return false;
  ordered_json nf = toOrdered(fields);
  ordered_json& c = (*o)["components"][component];
  if (c.contains("fields") && c["fields"] == nf) return true;
  pushUndo();
  c["fields"] = std::move(nf);
  changed();
  return true;
}

std::string SceneDocument::createObject(const std::string& name, const std::string& parentGuid,
                                        const std::vector<SceneComponent>& components) {
  if (mirror_) return std::string();  // F3.04: the document is frozen while playing
  if (!loaded_) return "";
  if (!parentGuid.empty() && indexOf(parentGuid) < 0) return "";
  pushUndo();
  std::string guid = generateGuid();
  ordered_json o = ordered_json::object();
  o["guid"] = guid;
  o["name"] = name;
  o["parent"] = parentGuid.empty() ? ordered_json(nullptr) : ordered_json(parentGuid);
  o["active"] = true;
  const float p[3] = {0, 0, 0}, r[4] = {0, 0, 0, 1}, s[3] = {1, 1, 1};
  ordered_json t = ordered_json::object();
  t["position"] = arrN(p, 3);
  t["rotation"] = arrN(r, 4);
  t["scale"] = arrN(s, 3);
  o["transform"] = std::move(t);
  ordered_json comps = ordered_json::array();
  for (const SceneComponent& c : components) {
    ordered_json cj = ordered_json::object();
    cj["type"] = c.type;
    cj["fields"] = toOrdered(c.fields);
    comps.push_back(std::move(cj));
  }
  o["components"] = std::move(comps);
  o["prefab"] = nullptr;
  if (!doc_.contains("objects") || !doc_["objects"].is_array()) doc_["objects"] = ordered_json::array();
  doc_["objects"].push_back(std::move(o));
  changed();
  return guid;
}

std::string SceneDocument::duplicate(const std::string& guid) {
  if (mirror_) return std::string();  // F3.04: the document is frozen while playing
  if (indexOf(guid) < 0) return "";
  std::vector<std::string> sub;
  collectSubtree(guid, sub);
  std::map<std::string, std::string> remap;
  for (const std::string& g : sub) remap[g] = generateGuid();
  pushUndo();
  std::vector<ordered_json> clones;
  for (const ordered_json& oj : doc_["objects"]) {  // keep file order
    std::string g = oj.value("guid", "");
    if (!remap.count(g)) continue;
    ordered_json c = oj;
    c["guid"] = remap[g];
    if (g == guid) {
      c["name"] = oj.value("name", "") + " (1)";
    } else if (c.contains("parent") && c["parent"].is_string()) {
      auto it = remap.find(c["parent"].get<std::string>());
      if (it != remap.end()) c["parent"] = it->second;
    }
    if (c.contains("components")) remapStrings(c["components"], remap);
    clones.push_back(std::move(c));
  }
  for (ordered_json& c : clones) doc_["objects"].push_back(std::move(c));
  changed();
  select(remap[guid]);
  return remap[guid];
}

bool SceneDocument::remove(const std::string& guid) {
  if (mirror_) return false;  // F3.04: the document is frozen while playing
  if (indexOf(guid) < 0) return false;
  std::vector<std::string> sub;
  collectSubtree(guid, sub);
  pushUndo();
  ordered_json kept = ordered_json::array();
  for (ordered_json& oj : doc_["objects"])
    if (std::find(sub.begin(), sub.end(), oj.value("guid", "")) == sub.end()) kept.push_back(std::move(oj));
  doc_["objects"] = std::move(kept);
  changed();
  return true;
}

bool SceneDocument::rename(const std::string& guid, const std::string& name) {
  if (mirror_) return false;  // F3.04: the document is frozen while playing
  ordered_json* o = findJson(guid);
  if (!o) return false;
  if (o->value("name", "") == name) return true;
  pushUndo();
  (*o)["name"] = name;
  changed();
  return true;
}

void SceneDocument::worldMatrix(const std::string& guid, double out[16]) const {
  std::vector<const SceneObject*> chain;
  std::string cur = guid;
  for (size_t guard = 0; guard <= live().objects.size(); ++guard) {
    const SceneObject* o = find(cur);
    if (!o) break;
    chain.push_back(o);
    if (o->parentGuid.empty()) break;
    cur = o->parentGuid;
  }
  Mat w = identity();
  for (auto it = chain.rbegin(); it != chain.rend(); ++it) w = mul(w, compose((*it)->position, (*it)->rotation, (*it)->scale));
  for (int i = 0; i < 16; ++i) out[i] = w.m[i];
}

bool SceneDocument::reparent(const std::string& guid, const std::string& newParent) {
  if (mirror_) return false;  // F3.04: the document is frozen while playing
  const SceneObject* o = find(guid);
  if (!o) return false;
  if (!newParent.empty() && (indexOf(newParent) < 0 || newParent == guid || isDescendant(newParent, guid))) return false;
  if (o->parentGuid == newParent) return true;
  Mat world{}, parentWorld = identity();
  worldMatrix(guid, world.m);
  if (!newParent.empty()) worldMatrix(newParent, parentWorld.m);
  Mat local = mul(invertAffine(parentWorld), world);
  float p[3], q[4], s[3];
  decompose(local, p, q, s);
  pushUndo();
  ordered_json* oj = findJson(guid);
  (*oj)["parent"] = newParent.empty() ? ordered_json(nullptr) : ordered_json(newParent);
  ordered_json& tr = (*oj)["transform"];
  if (!tr.is_object()) tr = ordered_json::object();
  tr["position"] = arrN(p, 3);
  tr["rotation"] = arrN(q, 4);
  tr["scale"] = arrN(s, 3);
  changed();
  return true;
}

bool SceneDocument::reparentMany(const std::vector<std::string>& guids, const std::string& newParent, const std::string& before) {
  if (!loaded_ || mirror_) return false;
  std::vector<std::string> top = topLevel(guids);
  if (top.empty()) return false;
  if (!newParent.empty() && indexOf(newParent) < 0) return false;
  for (const std::string& g : top)
    if (g == newParent || (!newParent.empty() && isDescendant(newParent, g))) return false;
  if (!before.empty()) {
    if (indexOf(before) < 0) return false;
    for (const std::string& g : top) if (g == before || isDescendant(before, g)) return false;
    const SceneObject* b = find(before);
    if (b->parentGuid != newParent && !(newParent.empty() && indexOf(b->parentGuid) < 0)) return false;  // must be a sibling
  }
  // new local transforms first, from the current hierarchy (world kept) for the objects that change parent
  struct Move { std::string guid; bool xform; float p[3], q[4], s[3]; };
  std::vector<Move> moves;
  Mat parentWorld = identity();
  if (!newParent.empty()) worldMatrix(newParent, parentWorld.m);
  const Mat invParent = invertAffine(parentWorld);
  for (const std::string& g : top) {
    const SceneObject* o = find(g);
    Move m{g, false, {}, {}, {}};
    if (o->parentGuid != newParent) {
      Mat world{};
      worldMatrix(g, world.m);
      decompose(mul(invParent, world), m.p, m.q, m.s);
      m.xform = true;
    }
    moves.push_back(m);
  }
  pushUndo();
  ordered_json& objs = doc_["objects"];
  std::vector<ordered_json> moved;
  for (const Move& m : moves) {
    size_t i = jsonIndex(m.guid);
    ordered_json oj = std::move(objs[i]);
    objs.erase(i);
    oj["parent"] = newParent.empty() ? ordered_json(nullptr) : ordered_json(newParent);
    if (m.xform) {
      ordered_json& tr = oj["transform"];
      if (!tr.is_object()) tr = ordered_json::object();
      tr["position"] = arrN(m.p, 3);
      tr["rotation"] = arrN(m.q, 4);
      tr["scale"] = arrN(m.s, 3);
    }
    moved.push_back(std::move(oj));
  }
  size_t at = objs.size();
  if (!before.empty()) at = jsonIndex(before);
  for (size_t i = 0; i < moved.size(); ++i) objs.insert(objs.begin() + static_cast<long>(at + i), std::move(moved[i]));
  if (doc_ == before_) { hasBefore_ = false; return true; }  // nothing changed (dropped where it already was)
  changed();
  return true;
}

std::vector<std::string> SceneDocument::duplicateMany(const std::vector<std::string>& guids) {
  std::vector<std::string> roots = topLevel(guids), result;
  if (roots.empty() || mirror_) return result;
  pushUndo();
  for (const std::string& root : roots) {
    std::vector<std::string> sub;
    collectSubtree(root, sub);
    std::map<std::string, std::string> remap;
    for (const std::string& g : sub) remap[g] = generateGuid();
    std::vector<ordered_json> clones;
    for (const ordered_json& oj : doc_["objects"]) {
      std::string g = oj.value("guid", "");
      if (!remap.count(g)) continue;
      ordered_json c = oj;
      c["guid"] = remap[g];
      if (g == root) c["name"] = oj.value("name", "") + " (1)";
      else if (c.contains("parent") && c["parent"].is_string()) {
        auto it = remap.find(c["parent"].get<std::string>());
        if (it != remap.end()) c["parent"] = it->second;
      }
      if (c.contains("components")) remapStrings(c["components"], remap);
      clones.push_back(std::move(c));
    }
    ordered_json& objs = doc_["objects"];
    size_t at = jsonIndex(root) + 1;  // right after the original: no sibling lies between them
    for (size_t i = 0; i < clones.size(); ++i) objs.insert(objs.begin() + static_cast<long>(at + i), std::move(clones[i]));
    result.push_back(remap[root]);
  }
  changed();
  setSelection(result);
  return result;
}

bool SceneDocument::removeMany(const std::vector<std::string>& guids) {
  std::vector<std::string> roots = topLevel(guids);
  if (roots.empty() || mirror_) return false;
  std::vector<std::string> sub;
  for (const std::string& r : roots) collectSubtree(r, sub);
  pushUndo();
  ordered_json kept = ordered_json::array();
  for (ordered_json& oj : doc_["objects"])
    if (std::find(sub.begin(), sub.end(), oj.value("guid", "")) == sub.end()) kept.push_back(std::move(oj));
  doc_["objects"] = std::move(kept);
  changed();
  return true;
}

std::string SceneDocument::copyObjects(const std::vector<std::string>& guids) const {
  std::vector<std::string> roots = topLevel(guids), sub;
  for (const std::string& r : roots) collectSubtree(r, sub);
  ordered_json out = ordered_json::object();
  out["rensga_clipboard"] = 1;
  out["roots"] = roots;
  ordered_json arr = ordered_json::array();
  if (doc_.contains("objects"))
    for (const ordered_json& oj : doc_["objects"])
      if (std::find(sub.begin(), sub.end(), oj.value("guid", "")) != sub.end()) arr.push_back(oj);
  out["objects"] = std::move(arr);
  return out.dump();
}

std::vector<std::string> SceneDocument::pasteObjects(const std::string& text, const std::string& parentGuid) {
  std::vector<std::string> result;
  if (!loaded_ || mirror_ || (!parentGuid.empty() && indexOf(parentGuid) < 0)) return result;
  ordered_json clip = ordered_json::parse(text, nullptr, false);
  if (clip.is_discarded() || !clip.is_object() || !clip.contains("objects") || !clip.contains("roots") ||
      !clip["objects"].is_array() || !clip["roots"].is_array())
    return result;
  std::map<std::string, std::string> remap;
  for (const ordered_json& oj : clip["objects"]) remap[oj.value("guid", "")] = generateGuid();
  std::vector<std::string> rootsOld = clip["roots"].get<std::vector<std::string>>();
  // roots keep their world transform only when pasted to the same parent; under another parent they keep the local one
  pushUndo();
  for (ordered_json c : clip["objects"]) {
    const std::string g = c.value("guid", "");
    c["guid"] = remap[g];
    const bool isRoot = std::find(rootsOld.begin(), rootsOld.end(), g) != rootsOld.end();
    if (isRoot) {
      c["parent"] = parentGuid.empty() ? ordered_json(nullptr) : ordered_json(parentGuid);
      c["name"] = c.value("name", "") + " (1)";
      result.push_back(remap[g]);
    } else if (c.contains("parent") && c["parent"].is_string()) {
      auto it = remap.find(c["parent"].get<std::string>());
      if (it != remap.end()) c["parent"] = it->second;
    }
    if (c.contains("components")) remapStrings(c["components"], remap);
    doc_["objects"].push_back(std::move(c));
  }
  changed();
  setSelection(result);
  return result;
}

}  // namespace editor
}  // namespace rensga
