#include "rensga/core/input.h"

#include <string.h>

#include "rensga/core/log.h"

namespace rensga {

namespace {
struct Action {
  char name[Input::kMaxNameLength] = {};
  PackInputKind kind = PackInputKind::Button;
  PackInputSource source = PackInputSource::South;
};
struct State {
  Action actions[Input::kMaxActions];
  u32 count = 0;
  VirtualGamepad pad, prev;
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
    default: return false;
  }
}

const Action* find(const char* name) {
  State& s = st();
  for (u32 i = 0; i < s.count; ++i)
    if (strcmp(s.actions[i].name, name) == 0) return &s.actions[i];
  return nullptr;
}

void define(const char* name, PackInputKind kind, PackInputSource src) {
  State& s = st();
  if (s.count >= Input::kMaxActions) {
    RLOG_ERROR("input", "too many input actions");
    return;
  }
  Action& a = s.actions[s.count++];
  strncpy(a.name, name, Input::kMaxNameLength - 1);
  a.name[Input::kMaxNameLength - 1] = 0;
  a.kind = kind;
  a.source = src;
}
}  // namespace

void Input::clearActions() { st().count = 0; }
void Input::defineButton(const char* name, PackInputSource s) { define(name, PackInputKind::Button, s); }
void Input::defineAxis2(const char* name, PackInputSource s) { define(name, PackInputKind::Axis2, s); }

void Input::loadActions(const PackView& pack, const PackInputAction* actions, u32 count) {
  clearActions();
  for (u32 i = 0; i < count; ++i)
    define(pack.string(actions[i].nameStr), static_cast<PackInputKind>(actions[i].kind),
           static_cast<PackInputSource>(actions[i].source));
}

void Input::update(const VirtualGamepad& pad) {
  State& s = st();
  s.prev = s.pad;
  s.pad = pad;
  // pressed = held now and not held last frame
  const bool* cur = reinterpret_cast<const bool*>(&s.pad.held);
  const bool* old = reinterpret_cast<const bool*>(&s.prev.held);
  bool* pr = reinterpret_cast<bool*>(&s.pad.pressed);
  bool* rel = reinterpret_cast<bool*>(&s.pad.released);
  for (size_t i = 0; i < sizeof(VirtualGamepad::Buttons); ++i) {
    pr[i] = cur[i] && !old[i];
    rel[i] = !cur[i] && old[i];
  }
}

Vector2 Input::GetAxis2(const char* name) {
  const Action* a = find(name);
  if (!a || a->kind != PackInputKind::Axis2) return Vector2();
  const VirtualGamepad& p = st().pad;
  if (a->source == PackInputSource::DPad) {
    return Vector2((p.held.right ? 1.0f : 0.0f) - (p.held.left ? 1.0f : 0.0f),
                   (p.held.up ? 1.0f : 0.0f) - (p.held.down ? 1.0f : 0.0f));
  }
  return Vector2(p.leftX, p.leftY);
}

Scalar Input::GetAxis(const char* name) {
  const Action* a = find(name);
  if (!a) return 0;
  if (a->kind == PackInputKind::Axis2) return GetAxis2(name).x;
  return buttonHeld(st().pad.held, a->source) ? 1.0f : 0.0f;
}

bool Input::GetButton(const char* name) {
  const Action* a = find(name);
  return a && a->kind == PackInputKind::Button && buttonHeld(st().pad.held, a->source);
}

bool Input::GetButtonDown(const char* name) {
  const Action* a = find(name);
  return a && a->kind == PackInputKind::Button && buttonHeld(st().pad.pressed, a->source);
}

bool Input::GetButtonUp(const char* name) {
  const Action* a = find(name);
  return a && a->kind == PackInputKind::Button && buttonHeld(st().pad.released, a->source);
}

Vector2 Input::GetRightStick() { return Vector2(st().pad.rightX, st().pad.rightY); }

const VirtualGamepad& Input::pad() { return st().pad; }

}  // namespace rensga
