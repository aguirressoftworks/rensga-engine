// F5.19: dev control channel for editor-driven Play on console (Pause / Resume / Step). On dev builds the editor writes
// commands to "host0:/rensga.ctl"; devControlPoll() reads them. Defined in engine/core/src/dev_control.cpp; the stub
// always answers "run one frame".
#pragma once
#include "rensga/core/types.h"

namespace rensga {
// Called once per loop iteration. Returns how many game frames to run now: 1 = normal, 0 = paused (draw only, do not
// advance), N > 1 = a step command (run N fixed frames and then answer 0 until a resume).
u32 devControlPoll();
}  // namespace rensga
