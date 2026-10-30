#include <cstring>
#include <map>

#include "drawers.h"

namespace rensga {
namespace editor {

bool drawScalarField(DrawCtx&, const nlohmann::json&, nlohmann::json&);
bool drawI32Field(DrawCtx&, const nlohmann::json&, nlohmann::json&);
bool drawBoolField(DrawCtx&, const nlohmann::json&, nlohmann::json&);
bool drawVector3Field(DrawCtx&, const nlohmann::json&, nlohmann::json&);
bool drawQuaternionField(DrawCtx&, const nlohmann::json&, nlohmann::json&);
bool drawColor32Field(DrawCtx&, const nlohmann::json&, nlohmann::json&);
bool drawGuidField(DrawCtx&, const nlohmann::json&, nlohmann::json&);
bool drawUnityEventField(DrawCtx&, const nlohmann::json&, nlohmann::json&);

namespace {
FieldDrawer g_byType[8] = {};
std::map<std::string, FieldDrawer>& byField() {
  static std::map<std::string, FieldDrawer> m;
  return m;
}
}  // namespace

void registerDrawer(FieldType t, FieldDrawer fn) { g_byType[static_cast<int>(t)] = fn; }

void registerFieldDrawer(const char* typeName, const char* fieldName, FieldDrawer fn) {
  byField()[std::string(typeName) + "." + fieldName] = fn;
}

void registerBuiltinDrawers() {
  static bool done = false;
  if (done) return;
  done = true;
  registerDrawer(FieldType::Scalar, drawScalarField);
  registerDrawer(FieldType::I32, drawI32Field);
  registerDrawer(FieldType::Bool, drawBoolField);
  registerDrawer(FieldType::Vector3, drawVector3Field);
  registerDrawer(FieldType::Quaternion, drawQuaternionField);
  registerDrawer(FieldType::Color32, drawColor32Field);
  registerDrawer(FieldType::Guid, drawGuidField);
  registerDrawer(FieldType::UnityEvent, drawUnityEventField);
}

FieldDrawer drawerFor(const char* typeName, const FieldDesc& f) {
  registerBuiltinDrawers();
  auto it = byField().find(std::string(typeName) + "." + f.name);
  if (it != byField().end()) return it->second;
  return g_byType[static_cast<int>(f.type)];
}

}  // namespace editor
}  // namespace rensga
