#include "rensga/core/components.h"

void rensga_reflect_register_components_h();  // generated
void rensga_reflect_register_gameplay_h();    // generated (gameplay.h, F2.05)
void rensga_reflect_register_rigidbody2d_h();  // generated (rigidbody2d.h, F4)
void rensga_reflect_register_physics_components_h();  // generated (F6: Rigidbody, CapsuleCollider)
void rensga_reflect_register_render_components_h();   // generated (F6: ParticleSystem, LineRenderer)
void rensga_reflect_register_animator_h();            // generated (F6: Animator)
void rensga_reflect_register_ui_components_h();       // generated (F6: Canvas, Slider, Toggle, LayoutGroup, EventSystem)

namespace rensga {

void registerBuiltinComponents() {
  // The table is generated from components.h by tools/reflect (components_h.reflect.gen.cpp); referencing it
  // here also keeps that object file linked when only World is used.
  ::rensga_reflect_register_components_h();
  ::rensga_reflect_register_gameplay_h();
  ::rensga_reflect_register_rigidbody2d_h();
  ::rensga_reflect_register_physics_components_h();
  ::rensga_reflect_register_render_components_h();
  ::rensga_reflect_register_animator_h();
  ::rensga_reflect_register_ui_components_h();
}

}  // namespace rensga
