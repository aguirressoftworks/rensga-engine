// F3.05: UnityEvent runtime (resolve by GUID, call through the method registry).
#include "rensga/core/unity_event.h"

#include <string.h>

#include "rensga/core/log.h"
#include "rensga/core/world.h"

namespace rensga {

MethodArg UnityEventListener::decode() const {
  MethodArg a;
  switch (static_cast<MethodArgKind>(argKind)) {
    case MethodArgKind::Scalar: memcpy(&a.s, arg, sizeof(a.s)); break;
    case MethodArgKind::I32: memcpy(&a.i, arg, sizeof(a.i)); break;
    case MethodArgKind::Bool: a.b = arg[0] != 0; break;
    case MethodArgKind::Object: memcpy(a.g.bytes, arg, 16); break;
    case MethodArgKind::None: break;
  }
  return a;
}
void UnityEventListener::setScalar(Scalar v) { argKind = static_cast<u8>(MethodArgKind::Scalar); memset(arg, 0, 16); memcpy(arg, &v, sizeof(v)); }
void UnityEventListener::setI32(i32 v) { argKind = static_cast<u8>(MethodArgKind::I32); memset(arg, 0, 16); memcpy(arg, &v, sizeof(v)); }
void UnityEventListener::setBool(bool v) { argKind = static_cast<u8>(MethodArgKind::Bool); memset(arg, 0, 16); arg[0] = v ? 1 : 0; }
void UnityEventListener::setObject(const Guid& g) { argKind = static_cast<u8>(MethodArgKind::Object); memcpy(arg, g.bytes, 16); }

u32 UnityEvent::Invoke() const {
  World* w = World::active();
  return w ? Invoke(*w) : 0;
}

u32 UnityEvent::Invoke(World& world) const {
  MethodArg none;
  return InvokeWith(world, MethodArgKind::None, none);
}

u32 UnityEvent::InvokeWith(MethodArgKind kind, const MethodArg& arg) const {
  World* w = World::active();
  return w ? InvokeWith(*w, kind, arg) : 0;
}

namespace {
// The event's value as the kind the method takes. Scalar <-> i32 truncates / widens, bool is "not zero".
MethodArg convertArg(MethodArgKind from, const MethodArg& a, MethodArgKind to) {
  MethodArg r = a;
  if (from == to) return r;
  const bool num = from == MethodArgKind::Scalar || from == MethodArgKind::I32 || from == MethodArgKind::Bool;
  if (!num) return r;
  const Scalar v = from == MethodArgKind::Scalar ? a.s : (from == MethodArgKind::I32 ? static_cast<Scalar>(a.i) : (a.b ? 1.0f : 0.0f));
  if (to == MethodArgKind::Scalar) r.s = v;
  else if (to == MethodArgKind::I32) r.i = static_cast<i32>(v);
  else if (to == MethodArgKind::Bool) r.b = v != 0;
  return r;
}
}  // namespace

u32 UnityEvent::InvokeWith(World& world, MethodArgKind dynKind, const MethodArg& dyn) const {
  u32 called = 0;
  const u32 n = count < kMaxUnityEventListeners ? count : kMaxUnityEventListeners;
  for (u32 i = 0; i < n; ++i) {
    const UnityEventListener& l = listeners[i];
    if (l.target.isNull()) continue;
    GameObject* go = world.findByGuid(l.target);
    const TypeInfo* t = TypeRegistry::instance().findByHash(l.typeHash);
    if (!go || !t) continue;
    const MethodInfo* m = MethodRegistry::instance().find(*t, l.methodHash);
    Component* c = m ? world.getComponent(go, *t) : nullptr;
    if (!c) continue;
    m->invoke(c, l.dynamic && dynKind != MethodArgKind::None && m->arg != MethodArgKind::None ? convertArg(dynKind, dyn, m->arg) : l.decode());
    ++called;
  }
  return called;
}

UnityEventListener* UnityEvent::AddListener(const Guid& target, const char* componentType, const char* method) {
  if (count >= kMaxUnityEventListeners || !componentType || !method) return nullptr;
  UnityEventListener& l = listeners[count++];
  l = UnityEventListener();
  l.target = target;
  l.typeHash = fnv1a32(componentType);
  l.methodHash = fnv1a32(method);
  return &l;
}

u32 UnityEvent::validate(World& world, const char* ownerName) const {
  u32 bad = 0;
  const u32 n = count < kMaxUnityEventListeners ? count : kMaxUnityEventListeners;
  for (u32 i = 0; i < n; ++i) {
    const UnityEventListener& l = listeners[i];
    if (l.target.isNull()) continue;
    const TypeInfo* t = TypeRegistry::instance().findByHash(l.typeHash);
    GameObject* go = world.findByGuid(l.target);
    const MethodInfo* m = t ? MethodRegistry::instance().find(*t, l.methodHash) : nullptr;
    if (!go || !t || !m || !world.getComponent(go, *t)) {
      char text[Guid::kTextLength + 1];
      l.target.format(text);
      RLOG_WARN("event", "UnityEvent on '%s': listener %u (target %s) does not resolve: %s", ownerName ? ownerName : "?", i, text,
                !go ? "object not in the scene" : !t ? "unknown component type" : !m ? "unknown method" : "component missing");
      ++bad;
    }
  }
  return bad;
}

}  // namespace rensga
