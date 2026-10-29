#include "rensga/core/components.h"

void rensga_reflect_register_components_h();  // generated

namespace rensga {

void registerBuiltinComponents() {
  // The table is generated from components.h by tools/reflect (components_h.reflect.gen.cpp); referencing it
  // here also keeps that object file linked when only World is used.
  ::rensga_reflect_register_components_h();
}

}  // namespace rensga
