#include "rensga/core/components.h"

void rensga_reflect_register_components_h();  // generated
void rensga_reflect_register_gameplay_h();    // generated (gameplay.h, F2.05)
void rensga_reflect_register_rigidbody2d_h();  // generated (rigidbody2d.h, F4)

namespace rensga {

void registerBuiltinComponents() {
  // The table is generated from components.h by tools/reflect (components_h.reflect.gen.cpp); referencing it
  // here also keeps that object file linked when only World is used.
  ::rensga_reflect_register_components_h();
  ::rensga_reflect_register_gameplay_h();
  ::rensga_reflect_register_rigidbody2d_h();
}

}  // namespace rensga
