#include "rensga/platform/touch.h"

namespace rensga {

namespace {
bool inside(const Rect& r, float x, float y, float slop) {
  return x >= r.x - slop && x < r.x + r.width + slop && y >= r.y - slop && y < r.y + r.height + slop;
}
}  // namespace

void TouchPad::layout() {
  const float vw = static_cast<float>(vw_), vh = static_cast<float>(vh_);
  const float m = vh / 32.0f;        // margin
  const float s = vh * 0.36f;        // d-pad square
  const float b = vh * 0.20f;        // face button
  zones_.dpad = Rect(m, vh - m - s, s, s);
  zones_.south = Rect(vw - m - 2.2f * b, vh - m - b, b, b);
  zones_.east = Rect(vw - m - b, vh - m - 1.8f * b, b, b);
  zones_.start = Rect(vw * 0.5f - 0.8f * b, m, 1.6f * b, 0.5f * b);
}

void TouchPad::setView(u32 virtualW, u32 virtualH, u32 windowW, u32 windowH) {
  if (virtualW != vw_ || virtualH != vh_) {
    vw_ = virtualW;
    vh_ = virtualH;
    layout();
  }
  ww_ = windowW;
  wh_ = windowH;
}

// Same fit as GlRenderer (computeLetterbox): whole-number scale when it fits, else aspect fit, centered.
void TouchPad::toVirtual(float nx, float ny, float* vx, float* vy) const {
  const float px = nx * static_cast<float>(ww_), py = ny * static_cast<float>(wh_);
  if (!vw_ || !vh_ || !ww_ || !wh_) {
    *vx = px;
    *vy = py;
    return;
  }
  u32 lw, lh;
  const u32 sx = ww_ / vw_, sy = wh_ / vh_, sc = sx < sy ? sx : sy;
  if (sc >= 1) {
    lw = vw_ * sc;
    lh = vh_ * sc;
  } else if (static_cast<u64>(ww_) * vh_ <= static_cast<u64>(wh_) * vw_) {
    lw = ww_;
    lh = static_cast<u32>(static_cast<u64>(ww_) * vh_ / vw_);
  } else {
    lh = wh_;
    lw = static_cast<u32>(static_cast<u64>(wh_) * vw_ / vh_);
  }
  if (lw < 1) lw = 1;
  if (lh < 1) lh = 1;
  const float ox = static_cast<float>((static_cast<i32>(ww_) - static_cast<i32>(lw)) / 2);
  const float oy = static_cast<float>((static_cast<i32>(wh_) - static_cast<i32>(lh)) / 2);
  *vx = (px - ox) * static_cast<float>(vw_) / static_cast<float>(lw);
  *vy = (py - oy) * static_cast<float>(vh_) / static_cast<float>(lh);
}

TouchPad::Finger* TouchPad::find(i64 id) {
  for (u32 i = 0; i < kMaxFingers; ++i)
    if (fingers_[i].active && !fingers_[i].released && fingers_[i].id == id) return &fingers_[i];
  return nullptr;
}

void TouchPad::fingerDown(i64 id, float nx, float ny) {
  used_ = true;
  Finger* f = find(id);
  if (!f)
    for (u32 i = 0; i < kMaxFingers; ++i)
      if (!fingers_[i].active) { f = &fingers_[i]; break; }
  if (!f) return;  // table full: extra fingers are ignored
  f->id = id;
  f->active = true;
  f->released = false;
  toVirtual(nx, ny, &f->x, &f->y);
}

void TouchPad::fingerMove(i64 id, float nx, float ny) {
  if (Finger* f = find(id)) toVirtual(nx, ny, &f->x, &f->y);
}

void TouchPad::fingerUp(i64 id) {
  if (Finger* f = find(id)) f->released = true;  // freed after the next apply()
}

void TouchPad::apply(VirtualGamepad& pad) {
  VirtualGamepad::Buttons& h = pad.held;
  const float slop = static_cast<float>(vh_) * 0.02f;
  for (u32 i = 0; i < kMaxFingers; ++i) {
    Finger& f = fingers_[i];
    if (!f.active) continue;
    if (inside(zones_.dpad, f.x, f.y, slop * 2)) {
      const float cx = zones_.dpad.x + zones_.dpad.width * 0.5f, cy = zones_.dpad.y + zones_.dpad.height * 0.5f;
      const float dx = f.x - cx, dy = f.y - cy, dead = zones_.dpad.width / 6.0f;
      if (dx < -dead) h.left = true;
      if (dx > dead) h.right = true;
      if (dy < -dead) h.up = true;
      if (dy > dead) h.down = true;
    }
    if (inside(zones_.south, f.x, f.y, slop)) h.south = true;
    else if (inside(zones_.east, f.x, f.y, slop)) h.east = true;
    if (inside(zones_.start, f.x, f.y, slop)) h.start = true;
    if (f.released) f.active = false;
  }
}

}  // namespace rensga
