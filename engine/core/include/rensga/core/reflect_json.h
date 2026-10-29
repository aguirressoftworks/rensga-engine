// MVP-13: JSON load/save of reflected fields. HOST ONLY (uses nlohmann/json and exceptions);
// the runtime reads binary blobs (reflect.h) instead. Library: rensga_reflect_json.
//
// Encoding: Scalar/I32 -> number, Bool -> bool, Vector3 -> [x,y,z], Quaternion -> [x,y,z,w],
// Guid -> "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx", Color32 -> [r,g,b,a]. Writing emits integers
// 0..255. Reading accepts integers 0..255, or floats 0..1 when any element is a float.
#pragma once
#include <string>

#include <nlohmann/json.hpp>

#include "rensga/core/reflect.h"

namespace rensga {

// {"fieldName": value, ...} for every reflected field of the object.
nlohmann::json fieldsToJson(const TypeInfo& type, const void* object);

// Applies the fields present in `fields` (absent ones keep their current/default value).
// Unknown field names and type mismatches fail and fill `error` (names the field).
bool fieldsFromJson(const TypeInfo& type, void* object, const nlohmann::json& fields, std::string* error = nullptr);

}  // namespace rensga
