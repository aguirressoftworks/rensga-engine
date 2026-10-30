#include "rensga/Sim.h"

#include <stdlib.h>
#include <string.h>

namespace rensga {

// ---- InputScript -----------------------------------------------------------

bool InputScript::add(u32 frame, Scalar x, Scalar y, u32 buttons, Scalar rx, Scalar ry, Scalar l2, Scalar r2) {
  if (count_ >= kMaxSteps) return false;
  steps_[count_++] = {frame, x, y, buttons, rx, ry, l2, r2};
  return true;
}

void InputScript::apply(u32 frame, VirtualGamepad& pad) const {
  const Step* cur = nullptr;
  for (u32 i = 0; i < count_; ++i)
    if (steps_[i].frame <= frame) cur = &steps_[i];
  pad = VirtualGamepad();
  if (!cur) return;
  pad.leftX = cur->x;
  pad.leftY = cur->y;
  pad.rightX = cur->rx;  // F3.06: the recorded snapshot carries the right stick and the analog triggers too
  pad.rightY = cur->ry;
  pad.l2Value = cur->l2;
  pad.r2Value = cur->r2;
  pad.held.l2 = cur->l2 >= 0.5f;
  pad.held.r2 = cur->r2 >= 0.5f;
  const u32 b = cur->buttons;
  pad.held.south = b & South; pad.held.east = b & East; pad.held.west = b & West; pad.held.north = b & North;
  pad.held.l = b & L; pad.held.r = b & R; pad.held.start = b & Start; pad.held.select = b & Select;
  pad.held.up = b & Up; pad.held.down = b & Down; pad.held.left = b & Left; pad.held.right = b & Right;
}

namespace {
const char* skipWs(const char* p, const char* end) {
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ',')) ++p;
  return p;
}
u32 buttonBit(const char* s, size_t n) {
  static const struct { const char* name; u32 bit; } kNames[] = {
      {"south", InputScript::South}, {"east", InputScript::East}, {"west", InputScript::West},
      {"north", InputScript::North}, {"l", InputScript::L}, {"r", InputScript::R},
      {"start", InputScript::Start}, {"select", InputScript::Select}, {"up", InputScript::Up},
      {"down", InputScript::Down}, {"left", InputScript::Left}, {"right", InputScript::Right}};
  for (const auto& e : kNames)
    if (strlen(e.name) == n && strncmp(e.name, s, n) == 0) return e.bit;
  return 0;
}
}  // namespace

bool InputScript::parse(const char* text, size_t size) {
  count_ = 0;
  const char* end = text + size;
  const char* p = static_cast<const char*>(memchr(text, '[', size));
  if (!p) return false;
  ++p;
  for (;;) {
    p = skipWs(p, end);
    if (p >= end || *p == ']') break;
    if (*p != '{') return false;
    ++p;
    Step st = {0, 0, 0, 0, 0, 0, 0, 0};
    for (;;) {
      p = skipWs(p, end);
      if (p >= end) return false;
      if (*p == '}') { ++p; break; }
      if (*p != '"') return false;
      const char* k = ++p;
      while (p < end && *p != '"') ++p;
      size_t kn = static_cast<size_t>(p - k);
      ++p;
      p = skipWs(p, end);
      if (p >= end || *p != ':') return false;
      p = skipWs(p + 1, end);
      if (kn == 7 && strncmp(k, "buttons", 7) == 0) {
        if (p >= end || *p != '[') return false;
        ++p;
        for (;;) {
          p = skipWs(p, end);
          if (p >= end) return false;
          if (*p == ']') { ++p; break; }
          if (*p != '"') return false;
          const char* s = ++p;
          while (p < end && *p != '"') ++p;
          st.buttons |= buttonBit(s, static_cast<size_t>(p - s));
          ++p;
        }
      } else {
        char* e = nullptr;
        double v = strtod(p, &e);
        if (e == p) return false;
        p = e;
        if (kn == 5 && strncmp(k, "frame", 5) == 0) st.frame = static_cast<u32>(v);
        else if (kn == 1 && *k == 'x') st.x = static_cast<Scalar>(v);
        else if (kn == 1 && *k == 'y') st.y = static_cast<Scalar>(v);
        else if (kn == 2 && strncmp(k, "rx", 2) == 0) st.rx = static_cast<Scalar>(v);
        else if (kn == 2 && strncmp(k, "ry", 2) == 0) st.ry = static_cast<Scalar>(v);
        else if (kn == 2 && strncmp(k, "l2", 2) == 0) st.l2 = static_cast<Scalar>(v);
        else if (kn == 2 && strncmp(k, "r2", 2) == 0) st.r2 = static_cast<Scalar>(v);
      }
    }
    if (!add(st.frame, st.x, st.y, st.buttons, st.rx, st.ry, st.l2, st.r2)) return false;
  }
  return true;
}

// ---- state hash and driver -------------------------------------------------

u32 liveCount(World& world, const char* typeName) {
  const TypeInfo* t = TypeRegistry::instance().find(typeName);
  const ComponentPool* p = t ? world.pool(*t) : nullptr;
  return p ? p->liveCount : 0;
}

u32 stateHash(World& world, u32 initialPickups) {
  u32 h = 2166136261u;
  auto mix = [&h](const void* data, size_t n) {
    const u8* b = static_cast<const u8*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 16777619u; }
  };
  for (u32 i = 0; i < world.objectHighWater(); ++i) {
    GameObject* go = world.objectAt(i);
    if (!go) continue;
    Vector3 p = go->transform.position();
    Quaternion q = go->transform.rotation();
    mix(&p, sizeof(p));
    mix(&q, sizeof(q));
  }
  u32 live = liveCount(world, "Pickup");
  u32 collected = initialPickups > live ? initialPickups - live : 0;
  mix(&collected, sizeof(collected));
  return h;
}

static unsigned bitsOf(float f) { u32 u; memcpy(&u, &f, 4); return static_cast<unsigned>(u); }

SimResult runSim(World& world, u32 frames, const InputScript* input, u32 initialPickups, const SimTraceOptions* trace) {
  physics::resetTriggerMemory();
  Gameplay2D::resetTriggerMemory();
  Input::update(VirtualGamepad());
  Input::update(VirtualGamepad());
  GameLoop loop;
  loop.init(world);
  loop.setFixedStepHook(physics::fixedStep, nullptr);
  VirtualGamepad pad;
  SimResult r;
  for (u32 f = 0; f < frames; ++f) {
    if (input) input->apply(f, pad);
    if (!loop.frame(1.0f / 60.0f, pad)) RLOG_ERROR("game", "rensga: heap allocation during frame %u", f);
    if (loop.frameCount() % 60 == 0) RLOG_INFO("game", "rensga: frame %u", static_cast<u32>(loop.frameCount()));
    if (trace && trace->trace) RLOG_INFO("game", "rensga: trace f=%u h=%08x", static_cast<unsigned>(f), static_cast<unsigned>(stateHash(world, initialPickups)));
    if (trace && trace->objFrame == static_cast<i64>(f)) {
      for (u32 i = 0; i < world.objectHighWater(); ++i) {
        GameObject* go = world.objectAt(i);
        if (!go) continue;
        Vector3 p = go->transform.position();
        Quaternion q = go->transform.rotation();
        RLOG_INFO("game", "rensga: obj f=%u i=%u %s p=%08x,%08x,%08x q=%08x,%08x,%08x,%08x", static_cast<unsigned>(f),
                  static_cast<unsigned>(i), go->name, bitsOf(p.x), bitsOf(p.y), bitsOf(p.z), bitsOf(q.x), bitsOf(q.y), bitsOf(q.z), bitsOf(q.w));
      }
    }
  }
  r.frames = frames;
  r.hash = stateHash(world, initialPickups);
  return r;
}

void defineDefaultInput() {
  Input::clearActions();
  Input::defineAxis2("Move", PackInputSource::Nub);
  Input::defineButton("Jump", PackInputSource::South);
  Input::defineButton("CameraLeft", PackInputSource::L);
  Input::defineButton("CameraRight", PackInputSource::R);
}

}  // namespace rensga
