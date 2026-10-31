// F6 contract stubs (C0) for API 7 rendering: Camera queries, ParticleSystem methods. Safe defaults (zeros, no camera) until B2
// implements them. The signatures are final (rensga/core/components.h, render_components.h).
#include "rensga/core/components.h"
#include "rensga/core/render_components.h"

namespace rensga {

// TODO(B2 rendering): main camera = enabled active Camera with the lowest depth; projection from fov / orthographicSize / viewport.
Camera* Camera::main() { return nullptr; }
Vector3 Camera::ScreenToWorldPoint(const Vector3&) const { return Vector3(); }
Vector3 Camera::WorldToScreenPoint(const Vector3&) const { return Vector3(); }
Ray Camera::ScreenPointToRay(const Vector2&) const { return Ray(); }
Vector3 Camera::WorldToViewportPoint(const Vector3&) const { return Vector3(); }
Vector3 Camera::ViewportToWorldPoint(const Vector3&) const { return Vector3(); }
Ray Camera::ViewportPointToRay(const Vector2&) const { return Ray(); }

// TODO(B2 rendering): fixed pool per system, emission, shape, over-lifetime curves, drawing on GL / GU / GS.
void ParticleSystem::Play() {}
void ParticleSystem::Stop() {}
void ParticleSystem::Emit(i32) {}
void ParticleSystem::Clear() { liveCount_ = 0; }

}  // namespace rensga
