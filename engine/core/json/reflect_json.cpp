#include "rensga/core/reflect_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rensga/core/unity_event.h"

namespace rensga {

namespace {
// ---- UnityEvent (F3.05): [{"target": guid, "type": name, "method": name, "arg": value}, ...] -----------------------
// Names come from the type / method registries; a hash with no registered name is written as "#xxxxxxxx" so a scene
// saved without its script module loaded keeps its listeners and round-trips.
std::string hashName(u32 hash, const char* name) {
  if (name) return name;
  char b[16];
  snprintf(b, sizeof(b), "#%08x", hash);
  return b;
}
bool nameHash(const std::string& s, u32& out) {
  if (s.size() == 9 && s[0] == '#') {
    char* end = nullptr;
    unsigned long v = strtoul(s.c_str() + 1, &end, 16);
    if (end != s.c_str() + 9) return false;
    out = static_cast<u32>(v);
    return true;
  }
  if (s.empty()) return false;
  out = fnv1a32(s.c_str());
  return true;
}

template <class json>
json eventToJson(const UnityEvent& e) {
  json arr = json::array();
  const u32 n = e.count < kMaxUnityEventListeners ? e.count : kMaxUnityEventListeners;
  for (u32 i = 0; i < n; ++i) {
    const UnityEventListener& l = e.listeners[i];
    json o = json::object();
    char text[Guid::kTextLength + 1];
    l.target.format(text);
    o["target"] = std::string(text);
    const TypeInfo* t = TypeRegistry::instance().findByHash(l.typeHash);
    o["type"] = hashName(l.typeHash, t ? t->name : nullptr);
    const MethodInfo* m = t ? MethodRegistry::instance().find(*t, l.methodHash) : nullptr;
    o["method"] = hashName(l.methodHash, m ? m->name : nullptr);
    MethodArg a = l.decode();
    switch (static_cast<MethodArgKind>(l.argKind)) {
      case MethodArgKind::Scalar: o["arg"] = a.s; break;
      case MethodArgKind::I32: o["arg"] = a.i; break;
      case MethodArgKind::Bool: o["arg"] = a.b; break;
      case MethodArgKind::Object: {
        a.g.format(text);
        o["arg"] = std::string(text);
        break;
      }
      case MethodArgKind::None: break;
    }
    arr.push_back(o);
  }
  return arr;
}

template <class json>
bool eventFromJson(const json& v, UnityEvent& out, std::string& why) {
  if (!v.is_array()) { why = "expected an array of listeners"; return false; }
  if (v.size() > kMaxUnityEventListeners) { why = "more than 4 listeners"; return false; }
  UnityEvent e;
  for (const json& o : v) {
    if (!o.is_object()) { why = "listener must be an object"; return false; }
    UnityEventListener l;
    if (!o.contains("target") || !o["target"].is_string() || !Guid::parse(o["target"].template get<std::string>().c_str(), l.target)) {
      why = "listener.target must be an object GUID string";
      return false;
    }
    if (!o.contains("type") || !o["type"].is_string() || !nameHash(o["type"].template get<std::string>(), l.typeHash)) {
      why = "listener.type must be a component type name";
      return false;
    }
    if (!o.contains("method") || !o["method"].is_string() || !nameHash(o["method"].template get<std::string>(), l.methodHash)) {
      why = "listener.method must be a method name";
      return false;
    }
    if (o.contains("arg")) {
      const json& a = o["arg"];
      if (a.is_boolean()) l.setBool(a.template get<bool>());
      else if (a.is_number_integer()) l.setI32(a.template get<i32>());
      else if (a.is_number()) l.setScalar(a.template get<Scalar>());
      else if (a.is_string()) {
        Guid g;
        if (!Guid::parse(a.template get<std::string>().c_str(), g)) { why = "listener.arg string must be a GUID"; return false; }
        l.setObject(g);
      } else { why = "listener.arg must be a number, bool or GUID string"; return false; }
      // A registered method decides the argument kind (an integer literal given to a Scalar method is a Scalar).
      const TypeInfo* t = TypeRegistry::instance().findByHash(l.typeHash);
      const MethodInfo* m = t ? MethodRegistry::instance().find(*t, l.methodHash) : nullptr;
      if (m && m->arg == MethodArgKind::Scalar && l.argKind == static_cast<u8>(MethodArgKind::I32)) {
        i32 iv = l.decode().i;
        l.setScalar(static_cast<Scalar>(iv));
      } else if (m && m->arg == MethodArgKind::I32 && l.argKind == static_cast<u8>(MethodArgKind::Scalar)) {
        l.setI32(static_cast<i32>(l.decode().s));
      }
    }
    e.listeners[e.count++] = l;
  }
  out = e;
  return true;
}

template <class json>
json colorToJson(const Color32& c) { return json::array({c.r, c.g, c.b, c.a}); }

template <class json>
json toJsonT(const TypeInfo& type, const void* object) {
  json out = json::object();
  const u8* base = static_cast<const u8*>(object);
  for (u32 i = 0; i < type.fieldCount; ++i) {
    const FieldDesc& f = type.fields[i];
    const u8* p = base + f.offset;
    switch (f.type) {
      case FieldType::Scalar: out[f.name] = *reinterpret_cast<const Scalar*>(p); break;
      case FieldType::I32: out[f.name] = *reinterpret_cast<const i32*>(p); break;
      case FieldType::Bool: out[f.name] = *reinterpret_cast<const bool*>(p); break;
      case FieldType::Vector3: {
        const Vector3* v = reinterpret_cast<const Vector3*>(p);
        out[f.name] = json::array({v->x, v->y, v->z});
        break;
      }
      case FieldType::Quaternion: {
        const Quaternion* q = reinterpret_cast<const Quaternion*>(p);
        out[f.name] = json::array({q->x, q->y, q->z, q->w});
        break;
      }
      case FieldType::Color32: out[f.name] = colorToJson<json>(*reinterpret_cast<const Color32*>(p)); break;
      case FieldType::Guid: {
        char text[Guid::kTextLength + 1];
        reinterpret_cast<const Guid*>(p)->format(text);
        out[f.name] = std::string(text);
        break;
      }
      case FieldType::UnityEvent: out[f.name] = eventToJson<json>(*reinterpret_cast<const UnityEvent*>(p)); break;
    }
  }
  return out;
}

template <class json>
bool numArray(const json& j, size_t n, Scalar* out) {
  if (!j.is_array() || j.size() != n) return false;
  for (size_t i = 0; i < n; ++i) {
    if (!j[i].is_number()) return false;
    out[i] = j[i].template get<Scalar>();
  }
  return true;
}

template <class json>
bool fromJsonT(const TypeInfo& type, void* object, const json& fields, std::string* error) {
  auto fail = [&](const std::string& field, const char* why) {
    if (error) *error = std::string(type.name) + "." + field + ": " + why;
    return false;
  };
  if (!fields.is_object()) return fail("", "fields must be an object");
  u8* base = static_cast<u8*>(object);
  for (auto it = fields.begin(); it != fields.end(); ++it) {
    const FieldDesc* f = nullptr;
    for (u32 i = 0; i < type.fieldCount; ++i)
      if (it.key() == type.fields[i].name) f = &type.fields[i];
    if (!f) return fail(it.key(), "unknown field");
    const json& v = it.value();
    u8* p = base + f->offset;
    switch (f->type) {
      case FieldType::Scalar:
        if (!v.is_number()) return fail(it.key(), "expected a number");
        *reinterpret_cast<Scalar*>(p) = v.template get<Scalar>();
        break;
      case FieldType::I32:
        if (!v.is_number_integer()) return fail(it.key(), "expected an integer");
        *reinterpret_cast<i32*>(p) = v.template get<i32>();
        break;
      case FieldType::Bool:
        if (!v.is_boolean()) return fail(it.key(), "expected true or false");
        *reinterpret_cast<bool*>(p) = v.template get<bool>();
        break;
      case FieldType::Vector3: {
        Scalar a[3];
        if (!numArray<json>(v, 3, a)) return fail(it.key(), "expected [x,y,z]");
        *reinterpret_cast<Vector3*>(p) = Vector3(a[0], a[1], a[2]);
        break;
      }
      case FieldType::Quaternion: {
        Scalar a[4];
        if (!numArray<json>(v, 4, a)) return fail(it.key(), "expected [x,y,z,w]");
        *reinterpret_cast<Quaternion*>(p) = Quaternion(a[0], a[1], a[2], a[3]);
        break;
      }
      case FieldType::Color32: {
        Scalar a[4];
        if (!numArray<json>(v, 4, a)) return fail(it.key(), "expected [r,g,b,a]");
        bool anyFloat = false;
        for (const json& e : v) anyFloat = anyFloat || e.is_number_float();
        Scalar k = anyFloat ? 255.0f : 1.0f;
        u8 c[4];
        for (int i = 0; i < 4; ++i) {
          Scalar x = Mathf::Clamp(a[i] * k + 0.5f, 0.0f, 255.0f);
          c[i] = static_cast<u8>(x);
        }
        *reinterpret_cast<Color32*>(p) = Color32(c[0], c[1], c[2], c[3]);
        break;
      }
      case FieldType::Guid: {
        Guid g;
        if (!v.is_string() || !Guid::parse(v.template get<std::string>().c_str(), g)) return fail(it.key(), "expected a GUID string");
        *reinterpret_cast<Guid*>(p) = g;
        break;
      }
      case FieldType::UnityEvent: {
        std::string why;
        UnityEvent e;
        if (!eventFromJson<json>(v, e, why)) return fail(it.key(), why.c_str());
        *reinterpret_cast<UnityEvent*>(p) = e;
        break;
      }
    }
  }
  return true;
}
}  // namespace

nlohmann::json fieldsToJson(const TypeInfo& t, const void* o) { return toJsonT<nlohmann::json>(t, o); }
nlohmann::ordered_json fieldsToOrderedJson(const TypeInfo& t, const void* o) { return toJsonT<nlohmann::ordered_json>(t, o); }
bool fieldsFromJson(const TypeInfo& t, void* o, const nlohmann::json& f, std::string* e) { return fromJsonT(t, o, f, e); }
bool fieldsFromJson(const TypeInfo& t, void* o, const nlohmann::ordered_json& f, std::string* e) { return fromJsonT(t, o, f, e); }

}  // namespace rensga
