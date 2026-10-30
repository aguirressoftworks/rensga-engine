// Shared by rensga_headless (Linux) and rensga_sim (PSP): the real GameLoop driven by a scripted input, with an optional
// per-frame state-hash trace so a determinism mismatch shows the first divergent frame and object (F2.G2).
//   "rensga: trace f=<n> h=<hex>"                     one line per frame (aggregate FNV-1a over every object's pose + collected count)
//   "rensga: obj f=<n> i=<idx> <name> <pos/rot bits>"  one line per live object, only for frame `objFrame`
// All values are printed as raw IEEE-754 bit patterns, so two runs are compared bit for bit.
#pragma once
#include <string.h>

#include "rensga/Sim.h"

namespace rensga {

struct SimTraceOptions {
  bool trace = false;
  i64 objFrame = -1;  // dump every object's bits after this frame (-1 = never)
};

namespace simtrace {
inline unsigned bitsOf(float f) { u32 u; memcpy(&u, &f, 4); return static_cast<unsigned>(u); }
}  // namespace simtrace

inline SimResult runSimTraced(World& world, u32 frames, const InputScript* input, u32 initialPickups,
                              const SimTraceOptions& opt) {
  physics::resetTriggerMemory();
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
    if (opt.trace) RLOG_INFO("game", "rensga: trace f=%u h=%08x", static_cast<unsigned>(f), static_cast<unsigned>(stateHash(world, initialPickups)));
    if (opt.objFrame == static_cast<i64>(f)) {
      for (u32 i = 0; i < world.objectHighWater(); ++i) {
        GameObject* go = world.objectAt(i);
        if (!go) continue;
        Vector3 p = go->transform.position();
        Quaternion q = go->transform.rotation();
        RLOG_INFO("game", "rensga: obj f=%u i=%u %s p=%08x,%08x,%08x q=%08x,%08x,%08x,%08x", static_cast<unsigned>(f),
                  static_cast<unsigned>(i), go->name, simtrace::bitsOf(p.x), simtrace::bitsOf(p.y), simtrace::bitsOf(p.z),
                  simtrace::bitsOf(q.x), simtrace::bitsOf(q.y), simtrace::bitsOf(q.z), simtrace::bitsOf(q.w));
      }
    }
  }
  r.frames = frames;
  r.hash = stateHash(world, initialPickups);
  return r;
}

}  // namespace rensga
