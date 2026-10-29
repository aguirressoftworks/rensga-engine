#include "rensga/core/reflect_json.h"

#include <string.h>

namespace rensga {

namespace {
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
