// F6.09 (B2, API 7): the Camera queries and the ParticleSystem methods. The math is in engine/core/src/render_view.cpp and is the
// same code renderWorld draws with, so ScreenToWorldPoint and the picture agree. Screen points are virtual pixels, origin top-left.
#include "rensga/core/components.h"
#include "rensga/core/render_components.h"
#include "rensga/core/render_view.h"
#include "rensga/core/world.h"

namespace rensga {

namespace {
CameraView viewOf(const Camera& c) { return makeCameraView(c, RenderScreen::width(), RenderScreen::height()); }
}  // namespace

// The enabled, active Camera with the lowest depth (the first one in creation order on a tie).
Camera* Camera::main() {
  World* w = World::active();
  if (!w) return nullptr;
  Camera* best = nullptr;
  w->forEach<Camera>([&](Camera& c) {
    GameObject* go = c.gameObject();
    if (!go || !c.enabled() || !go->activeInHierarchy()) return;
    if (!best || c.depth < best->depth) best = &c;
  });
  return best;
}
Vector3 Camera::ScreenToWorldPoint(const Vector3& p) const { return viewScreenToWorld(viewOf(*this), p); }
Vector3 Camera::WorldToScreenPoint(const Vector3& p) const { return viewWorldToScreen(viewOf(*this), p); }
Ray Camera::ScreenPointToRay(const Vector2& p) const { return viewScreenRay(viewOf(*this), p); }
Vector3 Camera::WorldToViewportPoint(const Vector3& p) const { return viewWorldToViewport(viewOf(*this), p); }
Vector3 Camera::ViewportToWorldPoint(const Vector3& p) const { return viewViewportToWorld(viewOf(*this), p); }
Ray Camera::ViewportPointToRay(const Vector2& p) const { return viewViewportRay(viewOf(*this), p); }

// Fixed pool per system, see engine/core/src/particles.cpp.
void ParticleSystem::Play() { Particles::play(*this); }
void ParticleSystem::Stop() { Particles::stop(*this); }
void ParticleSystem::Emit(i32 count) { Particles::emit(*this, count); }
void ParticleSystem::Clear() { Particles::clear(*this); }

}  // namespace rensga
