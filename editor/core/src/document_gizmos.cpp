#include "rensga/editor/document_gizmos.h"

#include <algorithm>
#include <cmath>

#include "rensga/core/gizmos.h"

namespace rensga {
namespace editor {

namespace {
float numf(const nlohmann::json& f, const char* key, float def) {
  auto it = f.find(key);
  return it != f.end() && it->is_number() ? it->get<float>() : def;
}

// A rectangle in the XY plane at z = 0 (2D pixel space), given by its min corner and size.
void rect(float x, float y, float w, float h) {
  const Vector3 a(x, y, 0), b(x + w, y, 0), c(x + w, y + h, 0), d(x, y + h, 0);
  Gizmos::DrawLine(a, b); Gizmos::DrawLine(b, c); Gizmos::DrawLine(c, d); Gizmos::DrawLine(d, a);
}

Matrix4 toMatrix(const M4& m) {
  Matrix4 r;
  for (int i = 0; i < 16; ++i) r.m[i] = m[i];
  return r;
}

bool vec3(const nlohmann::json& f, const char* key, float out[3], float def) {
  out[0] = out[1] = out[2] = def;
  auto it = f.find(key);
  if (it == f.end() || !it->is_array() || it->size() < 3) return false;
  for (int i = 0; i < 3; ++i) out[i] = (*it)[i].is_number() ? (*it)[i].get<float>() : def;
  return true;
}
}  // namespace

void buildDocumentGizmos(const SceneDocument& doc, const DocumentGizmoContext& ctx) {
  const DocumentGizmoStyle& st = ctx.style;
  const Matrix4 identity = Matrix4::identity();
  for (const SceneObject& o : doc.scene().objects) {
    if (!o.active) continue;
    const bool selected = doc.isSelected(o.guid);
    double wd[16];
    doc.worldMatrix(o.guid, wd);
    const M4 world = m4FromDouble(wd);
    const float px = world[12], py = world[13];
    for (const SceneComponent& c : o.components) {
      const nlohmann::json& f = c.fields;
      Gizmos::setMatrix(identity);
      if (c.type == "BoxCollider2D") {
        Gizmos::setColor(selected ? st.selection : (f.value("isTrigger", false) ? st.trigger : st.collider));
        rect(px + numf(f, "offsetX", 0), py + numf(f, "offsetY", 0), numf(f, "sizeX", 16), numf(f, "sizeY", 16));
      } else if (c.type == "Tilemap") {
        int w = 0, h = 0, ts = 0;
        const std::string map = f.value("map", "");
        if (ctx.tilemapSize && ctx.tilemapSize(map, w, h, ts)) {
          Gizmos::setColor(selected ? st.selection : st.tilemap);
          rect(px, py, static_cast<float>(w * ts), static_cast<float>(h * ts));
          if (selected) {  // tile grid
            Color faint = st.tilemap;
            faint.a = 0.35f;
            Gizmos::setColor(faint);
            for (int x = 1; x < w; ++x) Gizmos::DrawLine(Vector3(px + static_cast<float>(x * ts), py, 0), Vector3(px + static_cast<float>(x * ts), py + static_cast<float>(h * ts), 0));
            for (int y = 1; y < h; ++y) Gizmos::DrawLine(Vector3(px, py + static_cast<float>(y * ts), 0), Vector3(px + static_cast<float>(w * ts), py + static_cast<float>(y * ts), 0));
          }
        }
      } else if (c.type == "Camera2D") {
        const float vw = numf(f, "viewWidth", 0) > 0 ? numf(f, "viewWidth", 0) : ctx.viewW;
        const float vh = numf(f, "viewHeight", 0) > 0 ? numf(f, "viewHeight", 0) : ctx.viewH;
        // where the view starts: the target (plus offset) centered, then clamped to the bounds
        float cx = 0, cy = 0;
        const std::string target = f.value("target", "");
        if (const SceneObject* t = doc.find(target)) {
          double tw[16];
          doc.worldMatrix(t->guid, tw);
          cx = static_cast<float>(tw[12]) + numf(f, "targetOffsetX", 0);
          cy = static_cast<float>(tw[13]) + numf(f, "targetOffsetY", 0);
        }
        float vx = cx - vw * 0.5f, vy = cy - vh * 0.5f;
        float bx = numf(f, "boundsX", 0), by = numf(f, "boundsY", 0), bw = numf(f, "boundsW", 0), bh = numf(f, "boundsH", 0);
        const bool useBounds = f.value("useBounds", false);
        if (useBounds) {
          int tw = 0, th = 0, ts = 0;
          const std::string btm = f.value("boundsTilemap", "");
          if (const SceneObject* bo = doc.find(btm)) {
            for (const SceneComponent& bc : bo->components)
              if (bc.type == "Tilemap" && ctx.tilemapSize && ctx.tilemapSize(bc.fields.value("map", ""), tw, th, ts)) {
                double bwm[16];
                doc.worldMatrix(bo->guid, bwm);
                bx = static_cast<float>(bwm[12]); by = static_cast<float>(bwm[13]);
                bw = static_cast<float>(tw * ts); bh = static_cast<float>(th * ts);
              }
          }
          vx = std::max(bx, std::min(vx, bx + bw - vw));
          vy = std::max(by, std::min(vy, by + bh - vh));
          Gizmos::setColor(st.bounds);
          rect(bx, by, bw, bh);
        }
        Gizmos::setColor(selected ? st.selection : st.camera);
        rect(vx, vy, vw, vh);
        const float dw = numf(f, "deadZoneW", 0), dh = numf(f, "deadZoneH", 0);
        Gizmos::setColor(st.deadZone);
        rect(vx + (vw - dw) * 0.5f, vy + (vh - dh) * 0.5f, dw, dh);
      } else if (selected && c.type == "Camera") {
        Gizmos::setColor(st.camera);
        // the engine camera looks down its local -z: turn the +z frustum drawing half a circle about y
        Matrix4 turn = Matrix4::identity();
        turn.m[0] = -1; turn.m[10] = -1;
        Gizmos::setMatrix(toMatrix(world) * turn);
        const float far = std::min(numf(f, "farClip", 200), 30.0f);  // long frusta are cut for readability
        Gizmos::DrawFrustum(Vector3(0, 0, 0), numf(f, "fov", 60), far, std::max(numf(f, "nearClip", 0.3f), 0.05f),
                            ctx.viewH > 0 ? ctx.viewW / ctx.viewH : 1.7778f);
      } else if (selected && c.type == "DirectionalLight") {
        Gizmos::setColor(st.light);
        float d[3];
        vec3(f, "direction", d, 0);
        if (d[0] == 0 && d[1] == 0 && d[2] == 0) { d[1] = -1; }
        Gizmos::DrawRay(Vector3(px, py, world[14]), Vector3(d[0], d[1], d[2]).normalized() * 2.5f);
      } else if (c.type == "BoxCollider" && !ctx.is2D) {
        float ctr[3], size[3];
        vec3(f, "center", ctr, 0);
        vec3(f, "size", size, 1);
        Gizmos::setColor(selected ? st.selection : st.collider);
        Gizmos::setMatrix(toMatrix(world));
        Gizmos::DrawWireCube(Vector3(ctr[0], ctr[1], ctr[2]), Vector3(size[0], size[1], size[2]));
      } else if (c.type == "SphereCollider" && !ctx.is2D) {
        float ctr[3];
        vec3(f, "center", ctr, 0);
        Gizmos::setColor(selected ? st.selection : st.collider);
        Gizmos::setMatrix(toMatrix(world));
        Gizmos::DrawWireSphere(Vector3(ctr[0], ctr[1], ctr[2]), numf(f, "radius", 0.5f));
      } else if (c.type == "Light" && !ctx.is2D) {  // F6.02: the F5 Light
        // the light's own color (selected: the selection color so it reads as selected), through the document's 0..255 Color32
        Color lc = st.light;
        if (f.contains("color") && f["color"].is_array() && f["color"].size() >= 3)
          lc = Color(f["color"][0].get<float>() / 255.0f, f["color"][1].get<float>() / 255.0f, f["color"][2].get<float>() / 255.0f, 1);
        Gizmos::setColor(selected ? st.selection : lc);
        if (f.value("type", 0) == 1) {  // point: the range sphere (a faint dot when not selected)
          Matrix4 t = Matrix4::translation(Vector3(world[12], world[13], world[14]));
          Gizmos::setMatrix(t);
          const float range = std::max(numf(f, "range", 10), 0.01f);
          if (selected) {
            Gizmos::DrawWireSphere(Vector3(0, 0, 0), range);
            Gizmos::DrawWireSphere(Vector3(0, 0, 0), range * 0.5f);
          }
          Gizmos::DrawWireSphere(Vector3(0, 0, 0), 0.12f);
        } else {  // directional: parallel rays along the light's forward (+z) axis
          Gizmos::setMatrix(toMatrix(world));
          const float len = selected ? 3.0f : 1.2f;
          Gizmos::DrawRay(Vector3(0, 0, 0), Vector3(0, 0, len));
          if (selected)
            for (int i = 0; i < 4; ++i) {
              const float ox = (i & 1 ? 0.35f : -0.35f), oy = (i & 2 ? 0.35f : -0.35f);
              Gizmos::DrawRay(Vector3(ox, oy, 0), Vector3(0, 0, len));
            }
        }
      } else if (c.type == "AudioSource" && !ctx.is2D) {  // F6.02: its range, when the project's AudioSource has one (spatial audio fields)
        const float maxD = numf(f, "maxDistance", 0), minD = numf(f, "minDistance", 0);
        if (selected && maxD > 0) {
          Gizmos::setColor(st.selection);
          Gizmos::setMatrix(Matrix4::translation(Vector3(world[12], world[13], world[14])));
          Gizmos::DrawWireSphere(Vector3(0, 0, 0), maxD);
          if (minD > 0) Gizmos::DrawWireSphere(Vector3(0, 0, 0), minD);
        }
      } else if (c.type == "CapsuleCollider" && !ctx.is2D) {
        float ctr[3];
        vec3(f, "center", ctr, 0);
        const float r = std::max(numf(f, "radius", 0.5f), 0.01f), h = std::max(numf(f, "height", 2.0f), r * 2);
        const int dir = f.value("direction", 1);  // 0 X, 1 Y, 2 Z
        const float half = h * 0.5f - r;
        Vector3 axis(0, 1, 0), u(1, 0, 0), w(0, 0, 1);
        if (dir == 0) { axis = Vector3(1, 0, 0); u = Vector3(0, 1, 0); w = Vector3(0, 0, 1); }
        else if (dir == 2) { axis = Vector3(0, 0, 1); u = Vector3(1, 0, 0); w = Vector3(0, 1, 0); }
        const Vector3 c0(ctr[0], ctr[1], ctr[2]);
        const Vector3 a = c0 + axis * half, b = c0 - axis * half;
        Gizmos::setColor(selected ? st.selection : (f.value("isTrigger", false) ? st.trigger : st.collider));
        Gizmos::setMatrix(toMatrix(world));
        Gizmos::DrawWireSphere(a, r);
        Gizmos::DrawWireSphere(b, r);
        Gizmos::DrawLine(a + u * r, b + u * r);
        Gizmos::DrawLine(a - u * r, b - u * r);
        Gizmos::DrawLine(a + w * r, b + w * r);
        Gizmos::DrawLine(a - w * r, b - w * r);
      } else if (c.type == "Rigidbody" && selected && !ctx.is2D) {  // a gravity arrow (world down) and the body's axes
        Gizmos::setMatrix(identity);
        Gizmos::setColor(f.value("isKinematic", false) ? st.trigger : st.selection);
        if (f.value("useGravity", true) && !f.value("isKinematic", false))
          Gizmos::DrawRay(Vector3(world[12], world[13], world[14]), Vector3(0, -1.0f, 0));
        Gizmos::setMatrix(toMatrix(world));
        Gizmos::DrawLine(Vector3(0, 0, 0), Vector3(0.4f, 0, 0));
        Gizmos::DrawLine(Vector3(0, 0, 0), Vector3(0, 0.4f, 0));
        Gizmos::DrawLine(Vector3(0, 0, 0), Vector3(0, 0, 0.4f));
      } else if (selected && c.type == "SpriteRenderer") {  // selection outline
        Sprite2D s;
        s.model = world;
        s.texture = f.value("texture", "");
        s.srcW = f.value("srcW", 0);
        s.srcH = f.value("srcH", 0);
        s.pivotX = f.value("pivotX", 0);
        s.pivotY = f.value("pivotY", 0);
        float q[12];
        if (spriteWorldCorners(s, ctx.textureSize, q)) {
          Gizmos::setColor(st.selection);
          for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            Gizmos::DrawLine(Vector3(q[i * 3], q[i * 3 + 1], q[i * 3 + 2]), Vector3(q[j * 3], q[j * 3 + 1], q[j * 3 + 2]));
          }
        }
      }
    }
  }
  Gizmos::setMatrix(identity);
  Gizmos::setColor(Color::white());
}

}  // namespace editor
}  // namespace rensga
