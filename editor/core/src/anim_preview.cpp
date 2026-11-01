#include "rensga/editor/anim_preview.h"

#include <math.h>
#include <string.h>

#include "rensga/core/reflect.h"
#include "rensga/core/reflect_json.h"

namespace rensga {
namespace editor {

using nlohmann::json;

namespace {
json defaultsOf(const TypeInfo& ti) {
  json j = json::object();
  if (!ti.construct) return j;
  void* m = ::operator new(ti.size, std::align_val_t(ti.align));
  ti.construct(m);
  j = fieldsToJson(ti, m);
  if (ti.destruct) ti.destruct(m);
  ::operator delete(m, std::align_val_t(ti.align));
  return j;
}
std::string guidText(const Guid& g) {
  char t[Guid::kTextLength + 1];
  g.format(t);
  return t;
}
}  // namespace

SceneObject* AnimPreview::find(Scene& s, const std::string& guid) {
  for (SceneObject& o : s.objects) if (o.guid == guid) return &o;
  return nullptr;
}

SceneObject* AnimPreview::findPath(Scene& s, const std::string& path) {
  SceneObject* cur = find(s, root_);
  size_t p = 0;
  while (cur && p < path.size()) {
    size_t e = path.find('/', p);
    if (e == std::string::npos) e = path.size();
    const std::string name = path.substr(p, e - p);
    SceneObject* next = nullptr;
    for (SceneObject& o : s.objects) if (o.parentGuid == cur->guid && o.name == name) { next = &o; break; }
    cur = next;
    p = e + 1;
  }
  return cur;
}

bool AnimPreview::pathOf(const std::string& guid, std::string& out) const {
  out.clear();
  std::string cur = guid;
  for (int guard = 0; guard < 64; ++guard) {
    if (cur == root_) return true;
    const SceneObject* o = nullptr;
    for (const SceneObject& x : preview_.objects) if (x.guid == cur) { o = &x; break; }
    if (!o || o->parentGuid.empty()) return false;
    out = out.empty() ? o->name : o->name + "/" + out;
    cur = o->parentGuid;
  }
  return false;
}

bool AnimPreview::begin(SceneDocument& doc, const std::string& rootGuid, AnimClipDoc& clip, std::string& err) {
  if (active_) { err = "a preview is already active"; return false; }
  if (doc.playing()) { err = "stop Play first"; return false; }
  if (!doc.find(rootGuid)) { err = "the animated object does not exist"; return false; }
  baseline_ = doc.scene();
  preview_ = baseline_;
  root_ = rootGuid;
  clip_ = &clip;
  time_ = 0;
  recorded_ = 0;
  active_ = true;
  refresh();
  doc.setPlayMirror(&preview_, this);
  return true;
}

void AnimPreview::end(SceneDocument& doc) {
  if (!active_) return;
  doc.setPlayMirror(nullptr, nullptr);
  active_ = false;
  record_ = false;
  clip_ = nullptr;
}

void AnimPreview::setTime(double ticks) {
  time_ = ticks < 0 ? 0 : ticks;
  apply();
}

void AnimPreview::refresh() {
  bakedOk_ = clip_ && clip_->bake(baked_, message_);
  if (bakedOk_) message_.clear();
  apply();
}

void AnimPreview::apply() {
  if (!active_) return;
  // Keep the same Scene object (the document holds a pointer to it): copy the baseline over it element by element.
  preview_.objects = baseline_.objects;
  if (!bakedOk_) return;
  const PropClipSource src = baked_.source();
  const u64 tq = static_cast<u64>(time_ * 65536.0);
  std::vector<SceneObject*> quats;
  for (u32 ci = 0; ci < src.clip->curveCount; ++ci) {
    const PackPropCurve& pc = src.curves()[ci];
    SceneObject* o = findPath(preview_, src.str(pc.pathStr));
    if (!o) continue;
    const Scalar v = PropClips::sampleCurve(src, pc, tq);
    const FieldType vt = static_cast<FieldType>(pc.valueType);
    if (pc.componentType == kTransformTypeHash) {
      float* dst = nullptr;
      if (pc.fieldHash == fnv1a32("localPosition")) dst = o->position;
      else if (pc.fieldHash == fnv1a32("localScale")) dst = o->scale;
      else if (pc.fieldHash == fnv1a32("localRotation")) { dst = o->rotation; if (std::find(quats.begin(), quats.end(), o) == quats.end()) quats.push_back(o); }
      if (dst && pc.part >= 0 && pc.part < (dst == o->rotation ? 4 : 3)) dst[pc.part] = v;
      continue;
    }
    if (pc.componentType == kGameObjectTypeHash) {
      if (pc.fieldHash == fnv1a32("active")) o->active = v >= 0.5f;
      continue;
    }
    const TypeInfo* ti = TypeRegistry::instance().findByHash(pc.componentType);
    if (!ti) continue;
    const FieldDesc* fd = nullptr;
    for (u32 f = 0; f < ti->fieldCount; ++f) if (fnv1a32(ti->fields[f].name) == pc.fieldHash) fd = &ti->fields[f];
    SceneComponent* comp = nullptr;
    for (SceneComponent& c : o->components) if (c.type == ti->name) { comp = &c; break; }
    if (!fd || !comp) continue;
    json& fj = comp->fields;
    if (!fj.is_object()) fj = json::object();
    if (!fj.contains(fd->name)) fj[fd->name] = defaultsOf(*ti)[fd->name];
    json& val = fj[fd->name];
    switch (vt) {
      case FieldType::Scalar: val = v; break;
      case FieldType::I32: case FieldType::Enum: val = static_cast<long long>(v < 0 ? v - 0.5f : v + 0.5f); break;
      case FieldType::Bool: val = v >= 0.5f; break;
      case FieldType::Vector2: case FieldType::Vector3: case FieldType::Quaternion: case FieldType::Color32:
        if (val.is_array() && pc.part >= 0 && pc.part < static_cast<int>(val.size())) {
          if (vt == FieldType::Color32) val[pc.part] = static_cast<int>(v < 0 ? 0 : (v > 255 ? 255 : v + 0.5f));
          else val[pc.part] = v;
        }
        break;
      case FieldType::Guid: {
        const Guid* table = src.guids(pc);
        const u32 idx = static_cast<u32>(v);
        if (table && idx < pc.keyCount) val = guidText(table[idx]);
        break;
      }
      default: break;
    }
  }
  for (SceneObject* o : quats) {
    const float n = o->rotation[0] * o->rotation[0] + o->rotation[1] * o->rotation[1] + o->rotation[2] * o->rotation[2] + o->rotation[3] * o->rotation[3];
    if (n > 1e-12f) { const float k = 1.0f / sqrtf(n); for (float& c : o->rotation) c *= k; }
  }
}

bool AnimPreview::value(const std::string& target, const std::string& property, double* out) const {
  AnimPreview* self = const_cast<AnimPreview*>(this);
  SceneObject* o = self->findPath(self->preview_, target);
  if (!o) return false;
  const size_t dot = property.find('.');
  if (dot == std::string::npos) return false;
  const std::string comp = property.substr(0, dot), rest = property.substr(dot + 1);
  const size_t d2 = rest.find('.');
  const std::string field = rest.substr(0, d2);
  const char pc = d2 == std::string::npos ? 0 : rest[d2 + 1];
  const int part = pc == 'x' || pc == 'r' ? 0 : pc == 'y' || pc == 'g' ? 1 : pc == 'z' || pc == 'b' ? 2 : pc == 'w' || pc == 'a' ? 3 : -1;
  if (comp == "Transform") {
    if (part < 0) return false;
    if (field == "localPosition" && part < 3) { *out = o->position[part]; return true; }
    if (field == "localScale" && part < 3) { *out = o->scale[part]; return true; }
    if (field == "localRotation" && part < 4) { *out = o->rotation[part]; return true; }
    return false;
  }
  if (comp == "GameObject") { *out = o->active ? 1 : 0; return field == "active"; }
  for (const SceneComponent& c : o->components) {
    if (c.type != comp || !c.fields.is_object() || !c.fields.contains(field)) continue;
    const json& v = c.fields[field];
    if (v.is_number()) { *out = v.get<double>(); return true; }
    if (v.is_boolean()) { *out = v.get<bool>() ? 1 : 0; return true; }
    if (v.is_array() && part >= 0 && part < static_cast<int>(v.size()) && v[part].is_number()) { *out = v[part].get<double>(); return true; }
  }
  return false;
}

bool AnimPreview::keyChannel(const std::string& guid, const std::string& property, double v) {
  std::string path;
  if (!pathOf(guid, path)) { message_ = "the object is not inside the animated object"; return false; }
  const int c = clip_->addCurve(path, property);
  if (c < 0) { message_ = "cannot animate " + property; return false; }
  clip_->setKey(c, tickNow(), v);
  ++recorded_;
  return true;
}

bool AnimPreview::transform(const std::string& guid, const float pos[3], const float rot[4], const float scale[3]) {
  SceneObject* o = find(preview_, guid);
  if (!o) return false;
  if (!record_) {
    memcpy(o->position, pos, sizeof o->position); memcpy(o->rotation, rot, sizeof o->rotation); memcpy(o->scale, scale, sizeof o->scale);
    if (SceneObject* b = find(baseline_, guid)) { memcpy(b->position, pos, sizeof b->position); memcpy(b->rotation, rot, sizeof b->rotation); memcpy(b->scale, scale, sizeof b->scale); }
    return true;
  }
  static const char* xyz[3] = {"x", "y", "z"};
  static const char* xyzw[4] = {"x", "y", "z", "w"};
  bool any = false;
  for (int i = 0; i < 3; ++i)
    if (fabsf(pos[i] - o->position[i]) > 1e-6f) any = keyChannel(guid, std::string("Transform.localPosition.") + xyz[i], pos[i]) || any;
  for (int i = 0; i < 3; ++i)
    if (fabsf(scale[i] - o->scale[i]) > 1e-6f) any = keyChannel(guid, std::string("Transform.localScale.") + xyz[i], scale[i]) || any;
  bool rotChanged = false;
  for (int i = 0; i < 4; ++i) rotChanged = rotChanged || fabsf(rot[i] - o->rotation[i]) > 1e-6f;
  if (rotChanged) for (int i = 0; i < 4; ++i) any = keyChannel(guid, std::string("Transform.localRotation.") + xyzw[i], rot[i]) || any;
  if (any) refresh();
  return any;
}

bool AnimPreview::active(const std::string& guid, bool on) {
  SceneObject* o = find(preview_, guid);
  if (!o) return false;
  if (!record_) {
    o->active = on;
    if (SceneObject* b = find(baseline_, guid)) b->active = on;
    return true;
  }
  const bool ok = keyChannel(guid, "GameObject.active", on ? 1 : 0);
  if (ok) refresh();
  return ok;
}

bool AnimPreview::field(const std::string& guid, int component, const std::string& fieldName, const json& value) {
  SceneObject* o = find(preview_, guid);
  if (!o || component < 0 || component >= static_cast<int>(o->components.size())) return false;
  SceneComponent& comp = o->components[component];
  const TypeInfo* ti = TypeRegistry::instance().find(comp.type.c_str());
  const FieldDesc* fd = nullptr;
  if (ti) for (u32 f = 0; f < ti->fieldCount; ++f) if (fieldName == ti->fields[f].name) fd = &ti->fields[f];
  if (!record_ || !fd) {
    comp.fields[fieldName] = value;
    if (SceneObject* b = find(baseline_, guid))
      if (component < static_cast<int>(b->components.size())) b->components[component].fields[fieldName] = value;
    return true;
  }
  const std::string prop = comp.type + "." + fieldName;
  json old = comp.fields.is_object() && comp.fields.contains(fieldName) ? comp.fields[fieldName] : defaultsOf(*ti)[fieldName];
  bool any = false;
  switch (fd->type) {
    case FieldType::Scalar: case FieldType::I32: case FieldType::Enum:
      if (value.is_number()) any = keyChannel(guid, prop, value.get<double>());
      break;
    case FieldType::Bool:
      if (value.is_boolean()) any = keyChannel(guid, prop, value.get<bool>() ? 1 : 0);
      break;
    case FieldType::Vector2: case FieldType::Vector3: case FieldType::Quaternion: case FieldType::Color32: {
      static const char* xyzw[4] = {"x", "y", "z", "w"};
      static const char* rgba[4] = {"r", "g", "b", "a"};
      if (!value.is_array()) break;
      for (size_t i = 0; i < value.size() && i < 4; ++i) {
        const double nv = value[i].is_number() ? value[i].get<double>() : 0;
        const double ov = old.is_array() && i < old.size() && old[i].is_number() ? old[i].get<double>() : nv;
        if (fabs(nv - ov) > 1e-9) any = keyChannel(guid, prop + "." + (fd->type == FieldType::Color32 ? rgba[i] : xyzw[i]), nv) || any;
      }
      break;
    }
    case FieldType::Guid:
      if (value.is_string()) {
        std::string path;
        if (pathOf(guid, path)) {
          const int c = clip_->addCurve(path, prop);
          if (c >= 0) { clip_->setKeyGuid(c, tickNow(), value.get<std::string>()); ++recorded_; any = true; }
        }
      }
      break;
    default:
      message_ = std::string(fieldTypeName(fd->type)) + " fields cannot be animated";
      break;
  }
  if (any) refresh();
  return any;
}

}  // namespace editor
}  // namespace rensga
