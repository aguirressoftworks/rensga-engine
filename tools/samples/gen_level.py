#!/usr/bin/env python3
"""Deterministic generator for samples/level_walk (F6.02 exercise; Python 3 stdlib only, no ripped art).

A small gallery of three halls joined by doorways, built from three glTF files (one per material, the way a level is
exported from a modelling tool): level_floor.gltf, level_walls.gltf, level_props.gltf, with generated textures, three
.mat.json materials, collider objects and four scenes:

  Level         first-person walk, light baked into the vertex colors (MeshRenderer.bakeLighting)
  LevelRaw      the same level lit by the runtime lights only (at most 4 per object on the PSP)
  LevelBot      like Level, a script walks the route by itself (an emulator has no pad)
  LevelRawBot   like LevelRaw, walking by itself

Usage:
  python3 tools/samples/gen_level.py                 # writes samples/level_walk
  python3 tools/samples/gen_level.py --check         # regenerate into a temp dir and compare
  python3 tools/samples/gen_level.py --out DIR
"""
import argparse, json, math, os, sys, tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_blocky as gb  # Gltf writer, png, write, qaxis, r6, helpers

ROOT = gb.ROOT
G = gb.G
H = 3.0                      # wall height
HALF = 10.0                  # the floor spans -HALF..HALF in x and z
PARTITIONS = [               # (z, door x0, door x1)
    (2.0, 4.0, 6.0),
    (-4.0, -6.0, -4.0),
]
THICK = 0.4
WAYPOINTS = [(0.0, 8.0), (5.0, 4.0), (5.0, 0.0), (-5.0, -1.5), (-5.0, -6.0), (0.0, -8.0), (6.0, -8.0)]

LAMPS = [  # (x, y, z, rgb)
    (-6.0, 2.6, 6.0, (255, 200, 130)), (6.0, 2.6, 6.0, (255, 200, 130)),
    (-6.0, 2.6, -0.5, (255, 170, 110)), (6.0, 2.6, -0.5, (255, 220, 160)),
    (-6.0, 2.6, -7.0, (150, 190, 255)), (6.0, 2.6, -7.0, (255, 150, 110)),
    (0.0, 2.6, -7.0, (230, 230, 255)),
]
CRATES = [(-7.0, -8.0, 1.0), (-6.0, -8.2, 0.8), (7.5, -6.0, 1.0), (7.5, -4.8, 1.0), (8.2, -5.5, 1.0)]  # x, z, size
PILLARS = [(-6.0, 6.0), (0.0, 6.0), (6.0, 6.0), (-6.0, -1.0), (0.0, -1.0), (6.0, -1.0)]

GUID = {
    'floor': G('8000', 0x601), 'walls': G('8000', 0x602), 'props': G('8000', 0x603),
    'floor_png': G('8000', 0x604), 'walls_png': G('8000', 0x605), 'props_png': G('8000', 0x606),
    'mat_floor': G('8000', 0x607), 'mat_walls': G('8000', 0x608), 'mat_props': G('8000', 0x609),
    'scene_level': G('a000', 0x601), 'scene_raw': G('a000', 0x602), 'scene_bot': G('a000', 0x603), 'scene_rawbot': G('a000', 0x604),
}

# ---------------------------------------------------------------- textures (64x64, deterministic noise)
def rng(seed):
    s = [seed & 0xffffffff]
    def nxt():
        s[0] = (s[0] * 1664525 + 1013904223) & 0xffffffff
        return (s[0] >> 8) & 0xffff
    return nxt

def clamp8(v): return max(0, min(255, int(v)))

def tex_floor():
    r = rng(11); rgb = bytearray()
    for y in range(64):
        for x in range(64):
            mortar = x % 32 in (0, 31) or y % 32 in (0, 31)
            base = 150 if ((x // 32) + (y // 32)) % 2 == 0 else 128
            v = clamp8((52 if mortar else base) + (r() % 17) - 8)
            rgb += bytes((v, int(v * 0.97), int(v * 0.92)))
    return gb.png(64, 64, rgb)

def tex_walls():
    r = rng(23); rgb = bytearray()
    for y in range(64):
        row = y // 8
        for x in range(64):
            xs = (x + (row % 2) * 8) % 16
            n = (r() % 21) - 10
            if y % 8 == 0 or xs == 0:
                c = (170 + n // 2, 164 + n // 2, 150 + n // 2)
            else:
                shade = ((row * 5 + (x + (row % 2) * 8) // 16 * 3) % 4) * 6
                c = (150 + shade + n, 78 + shade // 2 + n // 2, 58 + shade // 3 + n // 2)
            rgb += bytes(clamp8(v) for v in c)
    return gb.png(64, 64, rgb)

def tex_props():
    r = rng(37); rgb = bytearray()
    for y in range(64):
        for x in range(64):
            plank = y // 16
            edge = y % 16 == 0 or (x % 32 == 0 and plank % 2 == 0)
            g = 100 + (plank % 2) * 14 + (r() % 15) - 7
            c = (36, 26, 18) if edge else (g * 0.62 + 40, g * 0.48 + 40, g * 0.31 + 40)
            rgb += bytes(clamp8(v) for v in c)
    return gb.png(64, 64, rgb)

# ---------------------------------------------------------------- geometry: 1 m cells; a texture tile spans 2 x 2 cells, so UVs stay in 0..1
class Mesh:
    def __init__(self):
        self.pos, self.nrm, self.uv, self.idx = [], [], [], []

    def quad(self, p0, p1, p2, p3, n, uvs):
        b = len(self.pos)
        for p, t in zip((p0, p1, p2, p3), uvs):
            self.pos.append([gb.r6(c) for c in p]); self.nrm.append([float(c) for c in n]); self.uv.append([gb.r6(t[0]), gb.r6(t[1])])
        self.idx.extend([b, b + 1, b + 2, b, b + 2, b + 3])

    def face(self, a, b, c, d, n, nu, nv, u0=0, v0=0):
        """A grid of nu x nv cells over the quad a-b-c-d (a->b along u, a->d along v), counter-clockwise seen from n."""
        def lerp(p, q, t): return [p[i] + (q[i] - p[i]) * t for i in range(3)]
        for i in range(nu):
            for j in range(nv):
                t0, t1, s0, s1 = i / nu, (i + 1) / nu, j / nv, (j + 1) / nv
                p00 = lerp(lerp(a, b, t0), lerp(d, c, t0), s0); p10 = lerp(lerp(a, b, t1), lerp(d, c, t1), s0)
                p11 = lerp(lerp(a, b, t1), lerp(d, c, t1), s1); p01 = lerp(lerp(a, b, t0), lerp(d, c, t0), s1)
                uu, vv = ((u0 + i) % 2) * 0.5, ((v0 + j) % 2) * 0.5
                self.quad(p00, p10, p11, p01, n, [(uu, vv), (uu + 0.5, vv), (uu + 0.5, vv + 0.5), (uu, vv + 0.5)])

    def box(self, lo, hi, faces='+x -x +y -y +z -z', cell=1.0):
        def cells(a, b): return max(1, int(round((b - a) / cell)))
        x0, y0, z0 = lo; x1, y1, z1 = hi
        nx, ny, nz = cells(x0, x1), cells(y0, y1), cells(z0, z1)
        ox, oy, oz = int(math.floor(x0)), int(math.floor(y0)), int(math.floor(z0))
        for f in faces.split():
            if f == '+y': self.face((x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0), (0, 1, 0), nx, nz, ox, oz)
            elif f == '-y': self.face((x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1), (0, -1, 0), nx, nz, ox, oz)
            elif f == '+x': self.face((x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (1, 0, 0), nz, ny, oz, oy)
            elif f == '-x': self.face((x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0), (-1, 0, 0), nz, ny, oz, oy)
            elif f == '+z': self.face((x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1), (0, 0, 1), nx, ny, ox, oy)
            elif f == '-z': self.face((x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0), (0, 0, -1), nx, ny, ox, oy)

def gltf_of(mesh, name, png_name):
    g = gb.Gltf(name + '.bin', png_name)
    A = g.attrs(mesh.pos, mesh.nrm, mesh.uv)
    ia = g.indices(mesh.idx)
    doc = {'scene': 0, 'scenes': [{'name': name, 'nodes': [0]}], 'nodes': [{'name': name, 'mesh': 0}],
           'meshes': [{'name': name, 'primitives': [{'attributes': A, 'indices': ia, 'material': 0, 'mode': 4}]}],
           'materials': [{'name': name, 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0.0, 'roughnessFactor': 1.0}}]}
    return g.finish(doc)

def build_meshes():
    floor, walls, props = Mesh(), Mesh(), Mesh()
    n, hh = int(2 * HALF), int(HALF)
    floor.face((-HALF, 0, HALF), (HALF, 0, HALF), (HALF, 0, -HALF), (-HALF, 0, -HALF), (0, 1, 0), n, n, -hh, -hh)   # the top only
    # outer walls: the inner faces
    walls.face((-HALF, 0, -HALF), (HALF, 0, -HALF), (HALF, H, -HALF), (-HALF, H, -HALF), (0, 0, 1), n, int(H), -hh, 0)     # south, faces +z
    walls.face((HALF, 0, HALF), (-HALF, 0, HALF), (-HALF, H, HALF), (HALF, H, HALF), (0, 0, -1), n, int(H), -hh, 0)        # north, faces -z
    walls.face((-HALF, 0, HALF), (-HALF, 0, -HALF), (-HALF, H, -HALF), (-HALF, H, HALF), (1, 0, 0), n, int(H), -hh, 0)     # west, faces +x
    walls.face((HALF, 0, -HALF), (HALF, 0, HALF), (HALF, H, HALF), (HALF, H, -HALF), (-1, 0, 0), n, int(H), -hh, 0)        # east, faces -x
    # partitions with a doorway (the lintel above it stays)
    for z, d0, d1 in PARTITIONS:
        for lo, hi in ((-HALF, d0), (d1, HALF)):
            walls.box((lo, 0, z - THICK / 2), (hi, H, z + THICK / 2), '+z -z +x -x +y')
        walls.box((d0, 2.2, z - THICK / 2), (d1, H, z + THICK / 2), '+z -z +y -y')
    for px, pz in PILLARS:
        props.box((px - 0.3, 0, pz - 0.3), (px + 0.3, H, pz + 0.3), '+x -x +z -z +y', 1.0)
    for cx, cz, s in CRATES:
        props.box((cx - s / 2, 0, cz - s / 2), (cx + s / 2, s, cz + s / 2), '+x -x +z -z +y', 0.5)
    props.box((-2.0, 0, -9.6), (2.0, 0.3, -8.4), '+x -x +z -z +y', 0.5)   # a low dais in the back hall
    return floor, walls, props

# ---------------------------------------------------------------- project files
def collider(n, name, lo, hi):
    c = [(lo[i] + hi[i]) / 2 for i in range(3)]; s = [hi[i] - lo[i] for i in range(3)]
    return gb.obj(n, name, tuple(gb.r6(v) for v in c), [('BoxCollider', {'center': [0, 0, 0], 'size': [gb.r6(v) for v in s], 'isTrigger': False})])

def build_scene(guid, ids, baked, auto):
    """ids: first object number of this scene (unique across the scenes of one pack)."""
    n = [ids]
    def nid():
        n[0] += 1
        return n[0] - 1
    objs = []
    objs.append(gb.obj(nid(), 'Camera', (WAYPOINTS[0][0], 1.6, WAYPOINTS[0][1]), [('Camera', {'fov': 65.0, 'nearClip': 0.2, 'farClip': 60.0})],
                       rot=gb.qaxis((0, 1, 0), 180.0)))
    objs.append(gb.obj(nid(), 'Player', (WAYPOINTS[0][0], 0.95, WAYPOINTS[0][1]), [   # the controller's origin is its centre
        ('CharacterController', {'height': 1.8, 'radius': 0.35, 'gravity': -20.0, 'stepOffset': 0.3}),
        ('LevelWalker', {'autoWalk': auto, 'moveSpeed': 3.0, 'turnSpeed': 100.0})]))
    mr = lambda mesh, mat: [('MeshRenderer', {'mesh': GUID[mesh], 'color': [255, 255, 255, 255], 'material': GUID[mat], 'bakeLighting': baked})]
    objs.append(gb.obj(nid(), 'Floor', (0, 0, 0), mr('floor', 'mat_floor')))
    objs.append(gb.obj(nid(), 'Walls', (0, 0, 0), mr('walls', 'mat_walls')))
    objs.append(gb.obj(nid(), 'Props', (0, 0, 0), mr('props', 'mat_props')))
    objs.append(gb.settings_obj(nid(), (46, 48, 62, 255), ([20, 22, 32, 255], 14.0, 40.0)))
    objs.append(gb.light(nid(), 'Moon', (0, 8, 0), 0, (150, 170, 230, 255), 0.35, 0.0, rot=gb.qaxis((1, 0, 0), 60.0)))
    for i, (x, y, z, col) in enumerate(LAMPS):
        objs.append(gb.light(nid(), 'Lamp%d' % i, (x, y, z), 1, col + (255,), 1.15, 9.5))
    objs.append(collider(nid(), 'ColFloor', (-HALF, -1.0, -HALF), (HALF, 0.0, HALF)))
    objs.append(collider(nid(), 'ColSouth', (-HALF - 1, 0, -HALF - 1), (HALF + 1, H, -HALF)))
    objs.append(collider(nid(), 'ColNorth', (-HALF - 1, 0, HALF), (HALF + 1, H, HALF + 1)))
    objs.append(collider(nid(), 'ColWest', (-HALF - 1, 0, -HALF), (-HALF, H, HALF)))
    objs.append(collider(nid(), 'ColEast', (HALF, 0, -HALF), (HALF + 1, H, HALF)))
    for k, (z, d0, d1) in enumerate(PARTITIONS):
        objs.append(collider(nid(), 'ColPartA%d' % k, (-HALF, 0, z - THICK / 2), (d0, H, z + THICK / 2)))
        objs.append(collider(nid(), 'ColPartB%d' % k, (d1, 0, z - THICK / 2), (HALF, H, z + THICK / 2)))
    for k, (px, pz) in enumerate(PILLARS):
        objs.append(collider(nid(), 'ColPillar%d' % k, (px - 0.3, 0, pz - 0.3), (px + 0.3, H, pz + 0.3)))
    for k, (cx, cz, s) in enumerate(CRATES):
        objs.append(collider(nid(), 'ColCrate%d' % k, (cx - s / 2, 0, cz - s / 2), (cx + s / 2, s, cz + s / 2)))
    return gb.scene(guid, objs)

SCRIPT = '''#include <rensga/Rensga.h>

using namespace rensga;

// F6.02 level_walk: first-person walk through the gallery with the pad only (no mouse; the PSP has none).
//   nub up / down : walk forward / back          nub left / right : turn
//   L / R         : strafe                         D-pad up / down  : look up / down
// With autoWalk a fixed route (the waypoints below, forth and back) drives the same code, so an emulator without a pad can
// walk the level. Logs "rensga: walker pos=<x>,<z> yaw=<deg> wp=<i>" about once a second.
struct LevelWalker : Behaviour {
  REFLECT(LevelWalker)
  SERIALIZE_FIELD(bool, autoWalk, false)
  RANGE(0, 10)
  SERIALIZE_FIELD(Scalar, moveSpeed, 3.0f)
  RANGE(10, 360)
  SERIALIZE_FIELD(Scalar, turnSpeed, 100.0f)
  RANGE(0, 2)
  TOOLTIP("Eye height above the character controller's centre (its origin).")
  SERIALIZE_FIELD(Scalar, eyeHeight, 0.7f)

  Scalar yaw = 0, pitch = 0;
  Vector2 stick;
  Scalar strafe = 0, tilt = 0;
  i32 wp = 1, dir = 1;
  u32 frame = 0;
  static constexpr i32 kWaypoints = %(count)d;

  static Vector3 waypoint(i32 i) {
    static const Scalar pts[kWaypoints][2] = {%(points)s};
    return Vector3(pts[i][0], 0, pts[i][1]);
  }

  void Start() override {
    // the player faces the first waypoint
    const Vector3 p = transform()->position();
    const Vector3 t = waypoint(1);
    yaw = Mathf::Atan2(t.x - p.x, t.z - p.z) * Mathf::Rad2Deg;
    Debug::Log("rensga: walker ready auto=%%d", autoWalk ? 1 : 0);
  }

  void Update() override {
    if (autoWalk) return;
    stick = Input::GetAxis2("Move");
    strafe = (Input::GetButton("StrafeRight") ? 1.0f : 0.0f) - (Input::GetButton("StrafeLeft") ? 1.0f : 0.0f);
    tilt = (Input::GetButton("LookUp") ? 1.0f : 0.0f) - (Input::GetButton("LookDown") ? 1.0f : 0.0f);
  }

  void FixedUpdate() override {
    CharacterController* cc = gameObject()->GetComponent<CharacterController>();
    if (!cc) return;
    const Scalar dt = Time::fixedDeltaTime();
    Vector3 pos = transform()->position();
    Scalar fwdIn = stick.y, rightIn = strafe;
    if (autoWalk) {
      const Vector3 t = waypoint(wp);
      Vector3 d = t - pos;
      d.y = 0;
      if (d.sqrMagnitude() < 0.25f) {
        wp += dir;
        if (wp >= kWaypoints) { dir = -1; wp = kWaypoints - 2; }
        if (wp < 0) { dir = 1; wp = 1; }
      }
      const Scalar want = Mathf::Atan2(d.x, d.z) * Mathf::Rad2Deg;
      Scalar diff = want - yaw;
      while (diff > 180.0f) diff -= 360.0f;
      while (diff < -180.0f) diff += 360.0f;
      const Scalar step = turnSpeed * dt;
      yaw += diff > step ? step : (diff < -step ? -step : diff);
      fwdIn = (diff > -35.0f && diff < 35.0f) ? 1.0f : 0.0f;
      rightIn = 0;
    } else {
      yaw += stick.x * turnSpeed * dt;
      pitch = Mathf::Clamp(pitch + tilt * 50.0f * dt, -50.0f, 50.0f);
    }
    const Scalar rad = yaw * Mathf::Deg2Rad;
    const Vector3 fwd(Mathf::Sin(rad), 0, Mathf::Cos(rad)), right(Mathf::Cos(rad), 0, -Mathf::Sin(rad));
    Vector3 move = fwd * fwdIn + right * rightIn;
    if (move.sqrMagnitude() > 1.0f) move = move.normalized();
    cc->Move(move * (moveSpeed * dt));
    transform()->setRotation(Quaternion::Euler(0, yaw, 0));
  }

  void LateUpdate() override {
    // the view looks down the camera's local -z, so the camera turns half a circle away from the player's forward
    if (GameObject* cam = GameObject::Find("Camera")) {
      cam->transform.setPosition(transform()->position() + Vector3(0, eyeHeight, 0));
      cam->transform.setRotation(Quaternion::Euler(-pitch, yaw + 180.0f, 0));
    }
    if (++frame %% 60 == 0) {
      const Vector3 p = transform()->position();
      Debug::Log("rensga: walker pos=%%d,%%d yaw=%%d wp=%%d", static_cast<int>(p.x * 10), static_cast<int>(p.z * 10), static_cast<int>(yaw), wp);
    }
  }
};
'''

def build_project():
    files = {}
    def add(rel, data): files[rel] = data
    floor, walls, props = build_meshes()
    stats = {}
    for key, mesh, pngf in (('floor', floor, tex_floor), ('walls', walls, tex_walls), ('props', props, tex_props)):
        add('Assets/Models/level_%s.gltf' % key, gltf_of(mesh, 'level_' + key, 'level_%s.png' % key))
        add('Assets/Models/level_%s.gltf.meta' % key, gb.meta(GUID[key]))
        add('Assets/Textures/level_%s.png' % key, pngf())
        add('Assets/Textures/level_%s.png.meta' % key, gb.meta(GUID[key + '_png']))
        add('Assets/Materials/%s.mat.json' % key, gb.J({'format': 1, 'texture': GUID[key + '_png'], 'color': [255, 255, 255, 255], 'unlit': False, 'alphaTest': False}))
        add('Assets/Materials/%s.mat.json.meta' % key, gb.meta(GUID['mat_' + key]))
        stats[key] = {'verts': len(mesh.pos), 'tris': len(mesh.idx) // 3}
    scenes = [('Level', 'scene_level', 0x100, True, False), ('LevelRaw', 'scene_raw', 0x200, False, False),
              ('LevelBot', 'scene_bot', 0x300, True, True), ('LevelRawBot', 'scene_rawbot', 0x400, False, True)]
    for name, key, ids, baked, auto in scenes:
        add('Assets/Scenes/%s.scene' % name, build_scene(GUID[key], ids, baked, auto))
        add('Assets/Scenes/%s.scene.meta' % name, gb.meta(GUID[key]))
    add('ProjectSettings/Project.json', gb.J({'format': 1, 'name': 'LevelWalk', 'target': 'psp', 'scenes': [GUID[s[1]] for s in scenes]}))
    ct = json.loads(open(os.path.join(ROOT, 'templates/psp-3d/ProjectSettings/ConsoleTarget.json')).read())
    ct['allowedComponents'] = ['MeshRenderer', 'Light', 'RenderSettings', 'Camera', 'DirectionalLight', 'CharacterController', 'BoxCollider',
                               'SphereCollider', 'CapsuleCollider', 'AudioSource', 'LevelWalker']
    add('ProjectSettings/ConsoleTarget.json', gb.J(ct))
    add('ProjectSettings/InputActions.json', gb.J({'format': 1, 'actions': [
        {'name': 'Move', 'kind': 'axis2', 'source': 'nub'},
        {'name': 'StrafeLeft', 'kind': 'button', 'source': 'l'}, {'name': 'StrafeRight', 'kind': 'button', 'source': 'r'},
        {'name': 'LookUp', 'kind': 'button', 'source': 'up'}, {'name': 'LookDown', 'kind': 'button', 'source': 'down'}]}))
    pts = ', '.join('{%.1ff, %.1ff}' % p for p in WAYPOINTS)
    add('Assets/Scripts/LevelWalker.cpp', SCRIPT % {'count': len(WAYPOINTS), 'points': pts})
    add('Assets/Scripts/LevelWalker.cpp.meta', gb.meta(G('b000', 0x601)))
    return files, stats

README = '''# level_walk

F6.02 (Editor 8, the 3D toolset) exercise project: a glTF level, materials, baked vertex lighting and a first-person walk.
Generated by `python3 tools/samples/gen_level.py` (own geometry, own textures; nothing ripped). `--check` fails when this folder is stale.

The level is three halls joined by two doorways (20 x 20 m, 3 m high), exported as three glTF files, one per material:
`level_floor.gltf`, `level_walls.gltf`, `level_props.gltf` (pillars, crates, a dais), with `floor`, `walls` and `props` materials.
Seven lamps (point Lights) and a dim moon (directional Light) light it; the PSP draws at most 4 lights per object.

| Scene | What it shows |
| --- | --- |
| `Level` | first-person walk, `bakeLighting` on every MeshRenderer: all lamps, shadows and ambient occlusion are in the vertex colors |
| `LevelRaw` | the same level lit by the runtime lights (4 per object) |
| `LevelBot` / `LevelRawBot` | the same two, walked by the `LevelWalker` script along a fixed route (autoWalk) |

Controls (`LevelWalker`): nub up/down walks, nub left/right turns, L/R strafe, D-pad up/down looks up/down.

## Try it

```bash
build/host/editor/app/rensga-editor --project samples/level_walk       # Scene view = the console preview; Window: Lighting > Bake all static meshes
tools/ci/psp_level.sh                                                  # cooks for psp (the bake runs in the cook), PPSSPPHeadless: LevelRawBot vs LevelBot screenshots
ctest --test-dir build/host -R toolset3d                               # editor tests: lit Scene view, materials, Lighting > Bake, walking the baked level
```
'''

def generate(out):
    files, stats = build_project()
    for rel, data in files.items(): gb.write(os.path.join(out, rel), data)
    gb.write(os.path.join(out, 'README.md'), README)
    return stats

def tree(d):
    r = {}
    for dp, dirs, fs in os.walk(d):
        dirs[:] = [x for x in dirs if x != 'Library']
        for f in fs: r[os.path.relpath(os.path.join(dp, f), d)] = open(os.path.join(dp, f), 'rb').read()
    return r

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'samples/level_walk'))
    ap.add_argument('--check', action='store_true')
    a = ap.parse_args()
    if a.check:
        with tempfile.TemporaryDirectory() as t:
            generate(t)
            want, got = tree(t), tree(a.out)
            bad = [rel for rel in want if got.get(rel) != want[rel]]
            for rel in bad: print('DIFF', rel)
            print('check: %s' % ('FAIL (%d)' % len(bad) if bad else 'OK, files identical'))
            return 1 if bad else 0
    print(json.dumps(generate(a.out)))
    return 0

if __name__ == '__main__':
    sys.exit(main())
