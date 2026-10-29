#include "rensga/core/components.h"

namespace rensga {

void registerBuiltinComponents() {
  // Static registrars in this TU have already run (inline static members); touching the
  // type info here makes the linker keep this file when only World is referenced.
  (void)MeshRenderer::staticTypeInfo();
  (void)BoxCollider::staticTypeInfo();
  (void)SphereCollider::staticTypeInfo();
  (void)CharacterController::staticTypeInfo();
  (void)Camera::staticTypeInfo();
  (void)DirectionalLight::staticTypeInfo();
}

}  // namespace rensga
