#include "rensga/core/input.h"

#include <string.h>

#include "rensga/core/log.h"

namespace rensga {

namespace {
struct VibrationState {
  Scalar low = 0, high = 0, left = 0;
  bool dirty = false;
};
struct State {
  InputAction actions[Input::kMaxActions];
  u32 count = 0;
  VirtualGamepad pad, prev;
  KeyState keysPrev;
  Scalar deadZone = 0.2f, outerClamp = 0.95f;
  bool rumble = false;
  VibrationState vib;
  IPlatform* platform = nullptr;
};
State& st() {
  static State s;
  return s;
}

bool buttonHeld(const VirtualGamepad::Buttons& b, PackInputSource s) {
  switch (s) {
    case PackInputSource::South: return b.south;
    case PackInputSource::East: return b.east;
    case PackInputSource::West: return b.west;
    case PackInputSource::North: return b.north;
    case PackInputSource::L: return b.l;
    case PackInputSource::R: return b.r;
    case PackInputSource::Start: return b.start;
    case PackInputSource::Select: return b.select;
    case PackInputSource::Up: return b.up;
    case PackInputSource::Down: return b.down;
    case PackInputSource::Left: return b.left;
    case PackInputSource::Right: return b.right;
    case PackInputSource::L2: return b.l2;
    case PackInputSource::R2: return b.r2;
    default: return false;
  }
}

// A binding: a pad button source, a key (kInputKeyBase + KeyCode) or kInputNone.
bool bindingHeld(const VirtualGamepad& p, u32 src) {
  if (src == kInputNone) return false;
  if (src >= kInputKeyBase) return p.keys.get(static_cast<KeyCode>(src - kInputKeyBase));
  return buttonHeld(p.held, static_cast<PackInputSource>(src));
}
bool anyBindingHeld(const VirtualGamepad& p, const u32* b, int n) {
  for (int i = 0; i < n; ++i)
    if (bindingHeld(p, b[i])) return true;
  return false;
}
bool hasBinding(const u32* b, int n) {
  for (int i = 0; i < n; ++i)
    if (b[i] != kInputNone) return true;
  return false;
}

// Unity's keyboard axis: moves toward the target at `sensitivity`, back to 0 at `gravity`; snap passes through 0 first.
Scalar ramp(Scalar cur, Scalar target, bool snap, Scalar gravity, Scalar sensitivity, Scalar dt) {
  if (target != 0) {
    if (snap && cur != 0 && (cur < 0) != (target < 0)) cur = 0;
    return Mathf::MoveTowards(cur, target, sensitivity * dt);
  }
  return Mathf::MoveTowards(cur, 0, gravity * dt);
}

Vector2 clampLength1(Vector2 v) {
  const Scalar m2 = v.x * v.x + v.y * v.y;
  if (m2 <= 1.0f) return v;
  const Scalar inv = 1.0f / Mathf::Sqrt(m2);
  return Vector2(v.x * inv, v.y * inv);
}

Scalar analogAxis(const VirtualGamepad& p, PackInputSource s) {
  switch (s) {
    case PackInputSource::AxisLeftX: return p.leftX;
    case PackInputSource::AxisLeftY: return p.leftY;
    case PackInputSource::AxisRightX: return p.rightX;
    case PackInputSource::AxisRightY: return p.rightY;
    case PackInputSource::AxisL2: return p.l2Value > 0 ? p.l2Value : (p.held.l2 ? 1.0f : 0.0f);
    case PackInputSource::AxisR2: return p.r2Value > 0 ? p.r2Value : (p.held.r2 ? 1.0f : 0.0f);
    default: return 0;
  }
}

}  // namespace

struct InputImpl {
static void evaluate(InputAction& a, const VirtualGamepad& p, Scalar dt) {
  a.prevPressed_ = a.pressed_;
  const bool snap = (a.flags_ & kInputFlagSnap) != 0;
  const bool invert = (a.flags_ & kInputFlagInvert) != 0;
  switch (a.kind_) {
    case PackInputKind::Button: {
      a.pressed_ = bindingHeld(p, a.source_) || bindingHeld(p, a.bind_[0]);
      a.value_ = a.raw_ = Vector2(a.pressed_ ? 1.0f : 0.0f, 0);
      return;
    }
    case PackInputKind::Axis: {
      const PackInputSource src = static_cast<PackInputSource>(a.source_);
      Scalar analog = analogAxis(p, src);
      if (a.dead_ > 0) {
        const Scalar m = Mathf::Abs(analog);
        analog = m <= a.dead_ ? 0.0f : (analog < 0 ? -1.0f : 1.0f) * (m - a.dead_) / (1.0f - a.dead_);
      }
      Scalar target = 0;
      if (hasBinding(a.bind_, 4)) {
        target = (anyBindingHeld(p, a.bind_, 2) ? 1.0f : 0.0f) - (anyBindingHeld(p, a.bind_ + 2, 2) ? 1.0f : 0.0f);
        a.ramp_.x = ramp(a.ramp_.x, target, snap, a.gravity_, a.sensitivity_, dt);
      }
      Scalar v = Mathf::Abs(a.ramp_.x) > Mathf::Abs(analog) ? a.ramp_.x : analog;
      Scalar raw = Mathf::Abs(target) > Mathf::Abs(analog) ? target : analog;
      if (invert) {
        v = -v;
        raw = -raw;
      }
      a.value_ = Vector2(v, 0);
      a.raw_ = Vector2(raw, 0);
      a.pressed_ = Mathf::Abs(v) >= 0.5f;
      return;
    }
    case PackInputKind::Axis2: {
      Vector2 base;
      switch (static_cast<PackInputSource>(a.source_)) {
        case PackInputSource::Nub: base = Vector2(p.leftX, p.leftY); break;
        case PackInputSource::RightStick: base = Vector2(p.rightX, p.rightY); break;
        case PackInputSource::DPad:
          base = Vector2((p.held.right ? 1.0f : 0.0f) - (p.held.left ? 1.0f : 0.0f),
                         (p.held.up ? 1.0f : 0.0f) - (p.held.down ? 1.0f : 0.0f));
          break;
        default: break;
      }
      Vector2 target, rampV;
      if (hasBinding(a.bind_, 8)) {
        target.y = (anyBindingHeld(p, a.bind_, 2) ? 1.0f : 0.0f) - (anyBindingHeld(p, a.bind_ + 2, 2) ? 1.0f : 0.0f);
        target.x = (anyBindingHeld(p, a.bind_ + 6, 2) ? 1.0f : 0.0f) - (anyBindingHeld(p, a.bind_ + 4, 2) ? 1.0f : 0.0f);
        target = clampLength1(target);  // diagonal keys: unit length, like a stick
        a.ramp_.x = ramp(a.ramp_.x, target.x, snap, a.gravity_, a.sensitivity_, dt);
        a.ramp_.y = ramp(a.ramp_.y, target.y, snap, a.gravity_, a.sensitivity_, dt);
        rampV = a.ramp_;
      }
      Vector2 v = clampLength1(Vector2(base.x + rampV.x, base.y + rampV.y));
      Vector2 raw = clampLength1(Vector2(base.x + target.x, base.y + target.y));
      if (invert) {
        v = -v;
        raw = -raw;
      }
      a.value_ = v;
      a.raw_ = raw;
      a.pressed_ = v.x * v.x + v.y * v.y >= 0.25f;
      return;
    }
  }
}

static InputAction* find(const char* name) {
  State& s = st();
  for (u32 i = 0; i < s.count; ++i)
    if (strcmp(s.actions[i].name_, name) == 0) return &s.actions[i];
  return nullptr;
}
};
namespace {
InputAction* findAction(const char* name) { return InputImpl::find(name); }
}  // namespace

InputAction* Input::define(const InputActionDef& def) {
  State& s = st();
  if (s.count >= kMaxActions) {
    RLOG_ERROR("input", "too many input actions");
    return nullptr;
  }
  InputAction& a = s.actions[s.count++];
  a = InputAction();
  strncpy(a.name_, def.name, kMaxNameLength - 1);
  a.name_[kMaxNameLength - 1] = 0;
  a.kind_ = def.kind;
  a.source_ = def.source;
  for (int i = 0; i < 8; ++i) a.bind_[i] = def.bind[i];
  a.gravity_ = def.gravity;
  a.sensitivity_ = def.sensitivity;
  a.dead_ = def.dead;
  a.flags_ = def.flags;
  return &a;
}

void Input::clearActions() { st().count = 0; }
void Input::defineButton(const char* name, PackInputSource source) {
  InputActionDef d;
  d.name = name;
  d.source = static_cast<u32>(source);
  define(d);
}
void Input::defineAxis2(const char* name, PackInputSource source) {
  InputActionDef d;
  d.name = name;
  d.kind = PackInputKind::Axis2;
  d.source = static_cast<u32>(source);
  define(d);
}
void Input::defineAxis(const char* name, PackInputSource source) {
  InputActionDef d;
  d.name = name;
  d.kind = PackInputKind::Axis;
  d.source = static_cast<u32>(source);
  define(d);
}

void Input::setStickSettings(Scalar deadZone, Scalar outerClamp) {
  State& s = st();
  s.deadZone = Mathf::Clamp(deadZone, 0.0f, 0.9f);
  s.outerClamp = Mathf::Clamp(outerClamp, s.deadZone + 0.05f, 1.0f);
}
Scalar Input::stickDeadZone() { return st().deadZone; }
Scalar Input::stickOuterClamp() { return st().outerClamp; }

void Input::loadActions(const PackView& pack, const PackInputAction* actions, u32 count) {
  clearActions();
  u32 extCount = 0, setCount = 0, ctCount = 0;
  const PackInputActionExt* ext = pack.records<PackInputActionExt>(pack_section::INPX, &extCount);
  const PackInputSettings* set = pack.records<PackInputSettings>(pack_section::INPS, &setCount);
  const PackConsoleTarget* ct = pack.records<PackConsoleTarget>(pack_section::CTGT, &ctCount);
  if (set && setCount > 0) setStickSettings(set->deadZone, set->outerClamp);
  else setStickSettings(0.2f, 0.95f);
  st().rumble = ct && ctCount > 0 && (ct->capabilities & kTargetCapRumble) != 0;
  st().vib = VibrationState();
  for (u32 i = 0; i < count; ++i) {
    InputActionDef d;
    d.name = pack.string(actions[i].nameStr);
    d.kind = static_cast<PackInputKind>(actions[i].kind);
    d.source = actions[i].source;
    if (ext && i < extCount) {
      for (int k = 0; k < 8; ++k) d.bind[k] = ext[i].bind[k];
      d.gravity = ext[i].gravity;
      d.sensitivity = ext[i].sensitivity;
      d.dead = ext[i].dead;
      d.flags = ext[i].flags;
    }
    define(d);
  }
}

Vector2 Input::ApplyRadialDeadZone(Vector2 v, Scalar dead, Scalar outer) {
  const Scalar m2 = v.x * v.x + v.y * v.y;
  if (m2 <= dead * dead) return Vector2();
  const Scalar m = Mathf::Sqrt(m2);
  const Scalar t = Mathf::Clamp01((m - dead) / (outer - dead));
  const Scalar k = t / m;
  return Vector2(v.x * k, v.y * k);
}

Scalar Input::stickFromByte(u8 value) {
  return Mathf::Clamp((static_cast<Scalar>(value) - 128.0f) / 127.0f, -1.0f, 1.0f);
}

void Input::update(const VirtualGamepad& pad, Scalar dt) {
  State& s = st();
  s.prev = s.pad;
  s.keysPrev = s.pad.keys;
  s.pad = pad;
  const Vector2 l = ApplyRadialDeadZone(Vector2(pad.leftX, pad.leftY), s.deadZone, s.outerClamp);
  const Vector2 r = ApplyRadialDeadZone(Vector2(pad.rightX, pad.rightY), s.deadZone, s.outerClamp);
  s.pad.leftX = l.x;
  s.pad.leftY = l.y;
  s.pad.rightX = r.x;
  s.pad.rightY = r.y;
  s.pad.l2Value = Mathf::Clamp01(pad.l2Value);
  s.pad.r2Value = Mathf::Clamp01(pad.r2Value);
  // pressed = held now and not held last frame
  const bool* cur = reinterpret_cast<const bool*>(&s.pad.held);
  const bool* old = reinterpret_cast<const bool*>(&s.prev.held);
  bool* pr = reinterpret_cast<bool*>(&s.pad.pressed);
  bool* rel = reinterpret_cast<bool*>(&s.pad.released);
  for (size_t i = 0; i < sizeof(VirtualGamepad::Buttons); ++i) {
    pr[i] = cur[i] && !old[i];
    rel[i] = !cur[i] && old[i];
  }
  for (u32 i = 0; i < s.count; ++i) InputImpl::evaluate(s.actions[i], s.pad, dt);
  if (s.vib.left > 0) {  // the effect ends here, not in the platform: platforms only see (low, high) changes
    s.vib.left -= dt;
    if (s.vib.left <= 0) {
      s.vib = VibrationState();
      s.vib.dirty = true;
    }
  }
}

// F6.09: the pointer is the pad's pointerX / pointerY / pointerDown; the previous frame's pad gives the edges.
bool Input::mousePresent() { return st().pad.pointerActive; }
Vector2 Input::mousePosition() { return st().pad.pointerActive ? Vector2(st().pad.pointerX, st().pad.pointerY) : Vector2(); }
bool Input::GetMouseButton(i32 button) { return button == 0 && st().pad.pointerActive && st().pad.pointerDown; }
bool Input::GetMouseButtonDown(i32 button) { return button == 0 && st().pad.pointerActive && st().pad.pointerDown && !(st().prev.pointerActive && st().prev.pointerDown); }
bool Input::GetMouseButtonUp(i32 button) { return button == 0 && st().prev.pointerActive && st().prev.pointerDown && !(st().pad.pointerActive && st().pad.pointerDown); }

Vector2 Input::GetAxis2(const char* name) {
  const InputAction* a = findAction(name);
  return a ? a->value_ : Vector2();
}
Scalar Input::GetAxis(const char* name) {
  const InputAction* a = findAction(name);
  return a ? a->value_.x : 0;
}
Scalar Input::GetAxisRaw(const char* name) {
  const InputAction* a = findAction(name);
  return a ? a->raw_.x : 0;
}
bool Input::GetButton(const char* name) {
  const InputAction* a = findAction(name);
  return a && a->IsPressed();
}
bool Input::GetButtonDown(const char* name) {
  const InputAction* a = findAction(name);
  return a && a->WasPressedThisFrame();
}
bool Input::GetButtonUp(const char* name) {
  const InputAction* a = findAction(name);
  return a && a->WasReleasedThisFrame();
}

bool Input::GetKey(KeyCode key) { return st().pad.keys.get(key); }
bool Input::GetKeyDown(KeyCode key) { return st().pad.keys.get(key) && !st().keysPrev.get(key); }
bool Input::GetKeyUp(KeyCode key) { return !st().pad.keys.get(key) && st().keysPrev.get(key); }
bool Input::anyKey() {
  const State& s = st();
  if (s.pad.keys.any()) return true;
  const bool* h = reinterpret_cast<const bool*>(&s.pad.held);
  for (size_t i = 0; i < sizeof(VirtualGamepad::Buttons); ++i)
    if (h[i]) return true;
  return false;
}
bool Input::anyKeyDown() {
  const State& s = st();
  for (int w = 0; w < 2; ++w)
    if (s.pad.keys.bits[w] & ~s.keysPrev.bits[w]) return true;
  const bool* p = reinterpret_cast<const bool*>(&s.pad.pressed);
  for (size_t i = 0; i < sizeof(VirtualGamepad::Buttons); ++i)
    if (p[i]) return true;
  return false;
}

Vector2 Input::GetRightStick() { return Vector2(st().pad.rightX, st().pad.rightY); }
Scalar Input::GetTrigger(bool right) {
  const VirtualGamepad& p = st().pad;
  const Scalar v = right ? p.r2Value : p.l2Value;
  return v > 0 ? v : ((right ? p.held.r2 : p.held.l2) ? 1.0f : 0.0f);
}
const VirtualGamepad& Input::pad() { return st().pad; }

InputAction* Input::Find(const char* name) { return findAction(name); }
u32 Input::actionCount() { return st().count; }
InputAction* Input::actionAt(u32 index) { return index < st().count ? &st().actions[index] : nullptr; }

bool Input::rumbleSupported() { return st().rumble; }
void Input::setRumbleSupported(bool supported) {
  st().rumble = supported;
  if (!supported) st().vib = VibrationState();
}
bool Input::SetVibration(Scalar low, Scalar high, Scalar seconds) {
  State& s = st();
  if (!s.rumble) return false;
  low = Mathf::Clamp01(low);
  high = Mathf::Clamp01(high);
  if (seconds <= 0 || (low <= 0 && high <= 0)) {
    low = high = seconds = 0;
  }
  s.vib.low = low;
  s.vib.high = high;
  s.vib.left = seconds;
  s.vib.dirty = true;
  return true;
}
void Input::currentVibration(Scalar& low, Scalar& high, Scalar& secondsLeft) {
  const VibrationState& v = st().vib;
  low = v.low;
  high = v.high;
  secondsLeft = v.left;
}
void Input::bindPlatform(IPlatform* platform) { st().platform = platform; }
void Input::flushVibration() {
  State& s = st();
  if (!s.vib.dirty) return;
  s.vib.dirty = false;
  if (s.platform) s.platform->setVibration(s.vib.low, s.vib.high, s.vib.left);
}

}  // namespace rensga
