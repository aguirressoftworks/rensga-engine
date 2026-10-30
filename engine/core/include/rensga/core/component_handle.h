// F3.05: a plain-data handle to a component (type, pool slot, generation). It reads as dead after the component is
// destroyed, even when the slot is reused. Coroutines, Invoke and Event use it to follow their owner.
#pragma once
#include "rensga/core/types.h"

namespace rensga {

class World;
class Component;

struct ComponentHandle {
  World* world = nullptr;
  u32 typeId = kInvalidIndex;
  u32 slot = 0;
  u32 generation = 0;
  bool isSet() const { return world != nullptr; }
  bool operator==(const ComponentHandle& o) const {
    return world == o.world && typeId == o.typeId && slot == o.slot && generation == o.generation;
  }
  // Defined in world.cpp. resolve() is null when the component was destroyed (or is being destroyed).
  Component* resolve() const;
  bool alive() const { return resolve() != nullptr; }
};

}  // namespace rensga
