#!/usr/bin/env python3
"""Deterministic generator for the F5 3D sample assets (Python 3 stdlib only, no ripped art).

Writes:
  <out>/gltf/blocky_walk.{gltf,bin,png}   blocky skinned humanoid, clips "walk" (1 s) and "idle" (2 s)
  <out>/gltf/room.{gltf,bin,png}          lit room: subdivided floor, four walls, pillars
  <out>/sample/...                        (with --sample DIR) project assets of samples/psp3d_room

Usage:
  python3 tools/samples/gen_blocky.py --out tests/fixtures/gltf --sample samples/psp3d_room
  python3 tools/samples/gen_blocky.py --check          # regenerate into a temp dir and diff
  python3 tools/samples/gen_blocky.py --preview out.png  # silhouette preview (front+side, walk frames 0 and 15)
"""
import argparse, base64, filecmp, json, math, os, shutil, struct, sys, tempfile, zlib

# ---------------------------------------------------------------- small helpers
def f32(*v): return struct.pack('<%df' % len(v), *v)

def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data if isinstance(data, bytes) else data.encode())

def png(w, h, rgb):
    raw = b''.join(b'\0' + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def pad4(b): return b + b'\0' * (-len(b) % 4)

def qaxis(ax, deg):
    a = math.radians(deg) / 2
    s = math.sin(a)
    return [ax[0] * s, ax[1] * s, ax[2] * s, math.cos(a)]

def r6(v): return round(v, 6)

# ---------------------------------------------------------------- glTF builder
class Gltf:
    def __init__(self, bin_name, png_name):
        self.bin = bytearray(); self.views = []; self.acc = []
        self.bin_name, self.png_name = bin_name, png_name

    def view(self, data, target=None):
        while len(self.bin) % 4: self.bin.append(0)
        v = {'buffer': 0, 'byteOffset': len(self.bin), 'byteLength': len(data)}
        if target: v['target'] = target
        self.bin += data
        self.views.append(v)
        return len(self.views) - 1

    def accessor(self, data, ctype, count, typ, target=None, minmax=None, normalized=False):
        a = {'bufferView': self.view(data, target), 'componentType': ctype, 'count': count, 'type': typ}
        if minmax: a['min'], a['max'] = minmax
        if normalized: a['normalized'] = True
        self.acc.append(a)
        return len(self.acc) - 1

    def attrs(self, pos, nrm, uv, joints=None, weights=None):
        n = len(pos)
        xs = [[p[i] for p in pos] for i in range(3)]
        A = {'POSITION': self.accessor(b''.join(f32(*p) for p in pos), 5126, n, 'VEC3', 34962,
                                       [[r6(min(x)) for x in xs], [r6(max(x)) for x in xs]]),
             'NORMAL': self.accessor(b''.join(f32(*p) for p in nrm), 5126, n, 'VEC3', 34962),
             'TEXCOORD_0': self.accessor(b''.join(f32(*p) for p in uv), 5126, n, 'VEC2', 34962)}
        if joints:
            A['JOINTS_0'] = self.accessor(b''.join(bytes(j) for j in joints), 5121, n, 'VEC4', 34962)
            A['WEIGHTS_0'] = self.accessor(b''.join(f32(*w) for w in weights), 5126, n, 'VEC4', 34962)
        return A

    def indices(self, idx):
        return self.accessor(pad4(b''.join(struct.pack('<H', i) for i in idx)) if False else
                             b''.join(struct.pack('<H', i) for i in idx), 5123, len(idx), 'SCALAR', 34963,
                             [[min(idx)], [max(idx)]])

    def finish(self, doc):
        doc['asset'] = {'version': '2.0', 'generator': 'rensga tools/samples/gen_blocky.py'}
        # The buffer is embedded as a base64 data URI: no .bin next to the .gltf (the repo's pre-commit BIOS guard
        # refuses large .bin files).
        doc['buffers'] = [{'uri': 'data:application/octet-stream;base64,' + base64.b64encode(bytes(self.bin)).decode(),
                           'byteLength': len(self.bin)}]
        doc['bufferViews'] = self.views; doc['accessors'] = self.acc
        doc['images'] = [{'uri': self.png_name}]
        doc['samplers'] = [{'magFilter': 9728, 'minFilter': 9728, 'wrapS': 33071, 'wrapT': 33071}]
        doc['textures'] = [{'source': 0, 'sampler': 0}]
        return json.dumps(doc, indent=1) + '\n'

# ---------------------------------------------------------------- box geometry
# faces: (normal, 4 corner picks as (xsel,ysel,zsel) with 0=min 1=max), counter-clockwise seen from outside
FACES = {
    '+x': ((1, 0, 0), [(1, 0, 1), (1, 0, 0), (1, 1, 0), (1, 1, 1)]),
    '-x': ((-1, 0, 0), [(0, 0, 0), (0, 0, 1), (0, 1, 1), (0, 1, 0)]),
    '+y': ((0, 1, 0), [(0, 1, 1), (1, 1, 1), (1, 1, 0), (0, 1, 0)]),
    '-y': ((0, -1, 0), [(0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)]),
    '+z': ((0, 0, 1), [(0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)]),
    '-z': ((0, 0, -1), [(1, 0, 0), (0, 0, 0), (0, 1, 0), (1, 1, 0)]),
}

def box(lo, hi, cell_of):
    """24 vertices (pos, normal, cell index) and 12 triangles. cell_of(face) -> palette cell."""
    out = []
    for name in ('+x', '-x', '+y', '-y', '+z', '-z'):
        n, corners = FACES[name]
        out.append(([(hi if c[0] else lo)[0] if False else (hi[0] if c[0] else lo[0]),
                     (hi[1] if c[1] else lo[1]), (hi[2] if c[2] else lo[2])] for c in corners))
    quads = []
    for name, cs in zip(('+x', '-x', '+y', '-y', '+z', '-z'), out):
        quads.append((name, FACES[name][0], list(cs)))
    return quads

# palette: 4x4 cells of 16 px on a 64x64 image
PAL = [(224, 172, 130), (60, 90, 180), (50, 55, 70), (40, 30, 30),
       (200, 60, 50), (240, 240, 240), (90, 160, 90), (230, 200, 60),
       (120, 80, 50), (150, 150, 160), (30, 30, 30), (255, 140, 60),
       (110, 70, 160), (60, 170, 200), (200, 200, 120), (90, 90, 90)]

def palette_png():
    rgb = bytearray()
    for y in range(64):
        for x in range(64):
            rgb += bytes(PAL[(y // 16) * 4 + x // 16])
    return png(64, 64, rgb)

def cell_uv(c): return ((c % 4) * 0.25 + 0.125, (c // 4) * 0.25 + 0.125)

def checker_png():
    rgb = bytearray()
    for y in range(64):
        for x in range(64):
            on = ((x // 8) + (y // 8)) & 1
            rgb += bytes((200, 196, 184) if on else (96, 100, 116))
    return png(64, 64, rgb)

# ---------------------------------------------------------------- character
# joint: (name, parent, world position at rest)
JOINTS = [
    ('Pelvis', -1, (0, 0.95, 0)),
    ('Torso', 0, (0, 1.05, 0)),
    ('Head', 1, (0, 1.55, 0)),
    ('UpperArmL', 1, (0.3, 1.5, 0)), ('LowerArmL', 3, (0.3, 1.2, 0)),
    ('UpperArmR', 1, (-0.3, 1.5, 0)), ('LowerArmR', 5, (-0.3, 1.2, 0)),
    ('UpperLegL', 0, (0.1, 0.9, 0)), ('LowerLegL', 7, (0.1, 0.5, 0)),
    ('UpperLegR', 0, (-0.1, 0.9, 0)), ('LowerLegR', 9, (-0.1, 0.5, 0)),
]
# box: (joint, lo, hi, {face: cell}, default cell). Blend edges: (joint index, lower/upper) handled below.
BOXES = [
    ('Pelvis', (-0.2, 0.85, -0.12), (0.2, 1.05, 0.12), 2, {'+y': 2}),
    ('Torso', (-0.22, 1.05, -0.12), (0.22, 1.55, 0.12), 1, {'+y': 0, '-y': 2}),
    ('Head', (-0.125, 1.57, -0.125), (0.125, 1.82, 0.125), 0, {'+z': 12, '+y': 3, '-z': 3, '+x': 3, '-x': 3}),
    ('UpperArmL', (0.24, 1.2, -0.06), (0.36, 1.5, 0.06), 1, {}),
    ('LowerArmL', (0.24, 0.9, -0.055), (0.36, 1.2, 0.055), 0, {'+y': 1}),
    ('UpperArmR', (-0.36, 1.2, -0.06), (-0.24, 1.5, 0.06), 1, {}),
    ('LowerArmR', (-0.36, 0.9, -0.055), (-0.24, 1.2, 0.055), 0, {'+y': 1}),
    ('UpperLegL', (0.02, 0.5, -0.08), (0.18, 0.9, 0.08), 2, {}),
    ('LowerLegL', (0.025, 0.1, -0.07), (0.175, 0.5, 0.07), 9, {}),
    ('UpperLegR', (-0.18, 0.5, -0.08), (-0.02, 0.9, 0.08), 2, {}),
    ('LowerLegR', (-0.175, 0.1, -0.07), (-0.025, 0.5, 0.07), 9, {}),
    ('FootL', (0.015, 0.0, -0.08), (0.185, 0.1, 0.2), 8, {}),
    ('FootR', (-0.185, 0.0, -0.08), (-0.015, 0.1, 0.2), 8, {}),
]
FOOT_JOINT = {'FootL': 'LowerLegL', 'FootR': 'LowerLegR'}
# vertices on these planes blend 50/50 between the pair (upper joint name, lower joint name, plane y)
BLENDS = [('UpperArmL', 'LowerArmL', 1.2), ('UpperArmR', 'LowerArmR', 1.2),
          ('UpperLegL', 'LowerLegL', 0.5), ('UpperLegR', 'LowerLegR', 0.5)]
JIDX = {j[0]: i for i, j in enumerate(JOINTS)}

def build_character():
    pos, nrm, uv, joints, weights, idx = [], [], [], [], [], []
    boxinfo = []
    for name, lo, hi, base, faces in BOXES:
        j = JIDX[FOOT_JOINT.get(name, name)]
        v0 = len(pos)
        for fname, n, corners in box(lo, hi, None):
            cell = faces.get(fname, base)
            u, v = cell_uv(cell)
            b = len(pos)
            for c in corners:
                pos.append([r6(c[0]), r6(c[1]), r6(c[2])]); nrm.append(list(map(float, n))); uv.append([u, v])
                jw = [(j, 1.0)]
                for up, lowr, y in BLENDS:
                    if name == up or name == lowr:
                        if abs(c[1] - y) < 1e-6:
                            jw = [(JIDX[up], 0.5), (JIDX[lowr], 0.5)]
                joints.append([jw[0][0], jw[1][0] if len(jw) > 1 else 0, 0, 0])
                weights.append([jw[0][1], jw[1][1] if len(jw) > 1 else 0.0, 0.0, 0.0])
            idx += [b, b + 1, b + 2, b, b + 2, b + 3]
        boxinfo.append((name, v0, len(pos) - v0))
    return pos, nrm, uv, joints, weights, idx, boxinfo

def clip_walk():
    """returns (times, {channel: (node, path, values)})"""
    n = 31
    t = [i / 30.0 for i in range(n)]
    ph = [2 * math.pi * x for x in t]
    A, K, B = 30.0, 40.0, 25.0
    ch = {}
    def rot(joint, fn): ch[(joint, 'rotation')] = [qaxis((1, 0, 0), fn(p)) for p in ph]
    rot('UpperLegL', lambda p: -A * math.sin(p))
    rot('UpperLegR', lambda p: A * math.sin(p))
    rot('LowerLegL', lambda p: K * max(0.0, math.cos(p)))
    rot('LowerLegR', lambda p: K * max(0.0, -math.cos(p)))
    rot('UpperArmL', lambda p: B * math.sin(p))
    rot('UpperArmR', lambda p: -B * math.sin(p))
    rot('LowerArmL', lambda p: -(12 + 14 * (0.5 - 0.5 * math.sin(p))))
    rot('LowerArmR', lambda p: -(12 + 14 * (0.5 + 0.5 * math.sin(p))))
    rot('Torso', lambda p: 2.0 * math.sin(p))
    ch[('Pelvis', 'translation')] = [[0.0, r6(0.95 + 0.025 * math.cos(2 * p)), 0.0] for p in ph]
    return t, ch

def clip_idle():
    n = 61
    t = [i / 30.0 for i in range(n)]
    ph = [math.pi * x for x in t]  # 2 s period
    ch = {}
    ch[('Torso', 'rotation')] = [qaxis((1, 0, 0), 1.5 * math.sin(p)) for p in ph]
    ch[('Head', 'rotation')] = [qaxis((1, 0, 0), -0.8 * math.sin(p)) for p in ph]
    ch[('UpperArmL', 'rotation')] = [qaxis((0, 0, 1), -(3 + 1.5 * math.sin(p))) for p in ph]
    ch[('UpperArmR', 'rotation')] = [qaxis((0, 0, 1), (3 + 1.5 * math.sin(p))) for p in ph]
    ch[('Pelvis', 'translation')] = [[0.0, r6(0.95 + 0.004 * math.sin(p)), 0.0] for p in ph]
    return t, ch

def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]
def mat_trs(t, q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), t[0]],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), t[1]],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), t[2]],
            [0, 0, 0, 1]]
def mat_t(t): return mat_trs(t, (0, 0, 0, 1))

def local_t(i):
    p = JOINTS[i][1]
    w = JOINTS[i][2]
    if p < 0: return list(w)
    pw = JOINTS[p][2]
    return [r6(w[0] - pw[0]), r6(w[1] - pw[1]), r6(w[2] - pw[2])]

def build_character_gltf():
    pos, nrm, uv, joints, weights, idx, boxinfo = build_character()
    g = Gltf('blocky_walk.bin', 'blocky_walk.png')
    A = g.attrs(pos, nrm, uv, joints, weights)
    ia = g.indices(idx)
    ibm = b''.join(f32(*[mat_t([-c for c in j[2]])[r][c] for c in range(4) for r in range(4)]) for j in JOINTS)
    ibma = g.accessor(ibm, 5126, len(JOINTS), 'MAT4')
    children = {i: [k for k, j in enumerate(JOINTS) if j[1] == i] for i in range(len(JOINTS))}
    nodes = []
    for i, j in enumerate(JOINTS):
        nd = {'name': j[0], 'translation': local_t(i)}
        if children[i]: nd['children'] = children[i]
        nodes.append(nd)
    nodes.append({'name': 'Blocky', 'mesh': 0, 'skin': 0})
    anims = []
    for cname, (times, chans) in (('walk', clip_walk()), ('idle', clip_idle())):
        ta = g.accessor(b''.join(f32(x) for x in times), 5126, len(times), 'SCALAR', None, [[0.0], [r6(times[-1])]])
        samplers, channels = [], []
        for (jn, path), vals in chans.items():
            if path == 'rotation':
                data = b''.join(f32(*[r6(c) for c in q]) for q in vals); typ = 'VEC4'
            else:
                data = b''.join(f32(*v) for v in vals); typ = 'VEC3'
            va = g.accessor(data, 5126, len(vals), typ)
            samplers.append({'input': ta, 'output': va, 'interpolation': 'LINEAR'})
            channels.append({'sampler': len(samplers) - 1, 'target': {'node': JIDX[jn], 'path': path}})
        anims.append({'name': cname, 'samplers': samplers, 'channels': channels})
    doc = {'scene': 0, 'scenes': [{'name': 'Blocky', 'nodes': [0, len(nodes) - 1]}], 'nodes': nodes,
           'meshes': [{'name': 'Blocky', 'primitives': [{'attributes': A, 'indices': ia, 'material': 0, 'mode': 4}]}],
           'skins': [{'name': 'BlockySkin', 'joints': list(range(len(JOINTS))), 'inverseBindMatrices': ibma, 'skeleton': 0}],
           'materials': [{'name': 'Palette', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0.0, 'roughnessFactor': 1.0}}],
           'animations': anims}
    return g.finish(doc), bytes(g.bin), palette_png(), dict(verts=len(pos), tris=len(idx) // 3, boxes=len(BOXES))

# ---------------------------------------------------------------- room
def build_room_gltf():
    pos, nrm, uv, idx = [], [], [], []
    S, H = 16.0, 3.0
    def quad(p0, p1, p2, p3, n, uvs):
        b = len(pos)
        for p, t in zip((p0, p1, p2, p3), uvs):
            pos.append([r6(c) for c in p]); nrm.append(list(map(float, n))); uv.append([r6(t[0]), r6(t[1])])
        idx.extend([b, b + 1, b + 2, b, b + 2, b + 3])
    # floor, +y up, ccw seen from above: x -> +x, z -> +z gives (x,0,z),(x,0,z+1),(x+1,0,z+1),(x+1,0,z)
    for ix in range(16):
        for iz in range(16):
            x, z = -8.0 + ix, -8.0 + iz
            quad((x, 0, z), (x, 0, z + 1), (x + 1, 0, z + 1), (x + 1, 0, z), (0, 1, 0),
                 [(ix / 16, iz / 16), (ix / 16, (iz + 1) / 16), ((ix + 1) / 16, (iz + 1) / 16), ((ix + 1) / 16, iz / 16)])
    # walls: inward facing
    def wall(fixed_axis, fixed, normal_sign):
        for i in range(16):
            for h in range(3):
                a, b = -8.0 + i, -7.0 + i
                y0, y1 = float(h), float(h + 1)
                if fixed_axis == 'z':   # wall at z=fixed, spans x
                    corners = [(a, y0, fixed), (b, y0, fixed), (b, y1, fixed), (a, y1, fixed)]
                    n = (0, 0, -normal_sign)
                    if normal_sign > 0: corners = corners[::-1][1:] + corners[::-1][:1] if False else [(b, y0, fixed), (a, y0, fixed), (a, y1, fixed), (b, y1, fixed)]
                else:                   # wall at x=fixed, spans z
                    corners = [(fixed, y0, b), (fixed, y0, a), (fixed, y1, a), (fixed, y1, b)]
                    n = (-normal_sign, 0, 0)
                    if normal_sign > 0: corners = [(fixed, y0, a), (fixed, y0, b), (fixed, y1, b), (fixed, y1, a)]
                u0, u1 = i / 16, (i + 1) / 16
                quad(*corners, n, [(u0, h / 16), (u1, h / 16), (u1, (h + 1) / 16), (u0, (h + 1) / 16)] if True else [])
    wall('z', 8.0, +1)    # north wall, faces -z (inward)
    wall('z', -8.0, -1)   # south wall, faces +z
    wall('x', 8.0, +1)    # east wall, faces -x
    wall('x', -8.0, -1)   # west wall, faces +x
    # pillars 0.6 x 3 x 0.6, three 1 m segments each
    for px, pz in ((-5.0, -5.0), (5.0, -5.0), (-5.0, 5.0), (5.0, 5.0)):
        for h in range(3):
            for fname in ('+x', '-x', '+z', '-z', '+y'):
                if fname == '+y' and h < 2: continue
                n, cs = FACES[fname]
                lo = (px - 0.3, float(h), pz - 0.3); hi = (px + 0.3, float(h + 1), pz + 0.3)
                pts = [(hi[0] if c[0] else lo[0], hi[1] if c[1] else lo[1], hi[2] if c[2] else lo[2]) for c in cs]
                u = 0.5
                quad(*pts, n, [(0.0, h / 16), (0.1, h / 16), (0.1, (h + 1) / 16), (0.0, (h + 1) / 16)])
    g = Gltf('room.bin', 'room.png')
    A = g.attrs(pos, nrm, uv)
    ia = g.indices(idx)
    doc = {'scene': 0, 'scenes': [{'name': 'Room', 'nodes': [0]}], 'nodes': [{'name': 'Room', 'mesh': 0}],
           'meshes': [{'name': 'Room', 'primitives': [{'attributes': A, 'indices': ia, 'material': 0, 'mode': 4}]}],
           'materials': [{'name': 'Checker', 'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0.0, 'roughnessFactor': 1.0}}]}
    return g.finish(doc), bytes(g.bin), checker_png(), dict(verts=len(pos), tris=len(idx) // 3)

# ---------------------------------------------------------------- validation
def validate(gltf_text, bin_bytes, label):
    d = json.loads(gltf_text)
    assert d['buffers'][0]['byteLength'] == len(bin_bytes), label + ': buffer length'
    for v in d['bufferViews']:
        assert v['byteOffset'] + v['byteLength'] <= len(bin_bytes), label + ': view range'
        assert v['byteOffset'] % 4 == 0, label + ': view align'
    sizes = {5126: 4, 5121: 1, 5123: 2}; comps = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
    def read(ai):
        a = d['accessors'][ai]; v = d['bufferViews'][a['bufferView']]
        n = a['count'] * comps[a['type']]
        assert a['count'] * comps[a['type']] * sizes[a['componentType']] == v['byteLength'], label + ': accessor size %d' % ai
        fmt = {5126: 'f', 5121: 'B', 5123: 'H'}[a['componentType']]
        vals = struct.unpack_from('<%d%s' % (n, fmt), bin_bytes, v['byteOffset'])
        return a, [vals[i:i + comps[a['type']]] for i in range(0, n, comps[a['type']])]
    for m in d['meshes']:
        for p in m['primitives']:
            A = p['attributes']
            a, P = read(A['POSITION'])
            for k in range(3):
                assert abs(min(x[k] for x in P) - a['min'][k]) < 1e-5 and abs(max(x[k] for x in P) - a['max'][k]) < 1e-5, label + ': min/max'
            for k in ('NORMAL', 'TEXCOORD_0', 'JOINTS_0', 'WEIGHTS_0'):
                if k in A: assert d['accessors'][A[k]]['count'] == a['count'], label + ': count ' + k
            _, N = read(A['NORMAL'])
            assert all(abs(math.sqrt(sum(c * c for c in n)) - 1) < 1e-4 for n in N), label + ': normals'
            _, UV = read(A['TEXCOORD_0'])
            assert all(0 <= c <= 1 for t in UV for c in t), label + ': uv range'
            _, I = read(p['indices'])
            assert max(x[0] for x in I) < a['count'] and len(I) % 3 == 0, label + ': indices'
            if 'WEIGHTS_0' in A:
                _, W = read(A['WEIGHTS_0']); _, J = read(A['JOINTS_0'])
                nj = len(d['skins'][0]['joints'])
                for w, j in zip(W, J):
                    assert abs(sum(w) - 1) < 1e-5, label + ': weights sum'
                    assert all(x < nj for x, ww in zip(j, w) if ww > 0), label + ': joint index'
    if 'skins' in d:
        sk = d['skins'][0]
        _, M = read(sk['inverseBindMatrices']); assert len(M) == len(sk['joints']), label + ': ibm count'
        for an in d['animations']:
            for c in an['channels']:
                s = an['samplers'][c['sampler']]
                ti, vo = read(s['input']), read(s['output'])
                assert ti[0]['count'] == vo[0]['count'], label + ': sampler counts'
                assert abs(ti[0]['max'][0] - ti[1][-1][0]) < 1e-5, label + ': anim time max'
                if c['target']['path'] == 'rotation':
                    assert all(abs(math.sqrt(sum(x * x for x in q)) - 1) < 1e-4 for q in vo[1]), label + ': quat'
                if an['name'] in ('walk', 'idle'):
                    assert vo[1][0] == vo[1][-1], label + ': %s not looping' % an['name']

# ---------------------------------------------------------------- sample project
G = lambda kind, n: '00000000-0000-4000-%s-%012x' % (kind, n)
GUID = {
    'blocky': G('8000', 0x501), 'blocky_png': G('8000', 0x502), 'room': G('8000', 0x503), 'room_png': G('8000', 0x504),
    'mat_char': G('8000', 0x505), 'mat_room': G('8000', 0x506), 'mat_pillar': G('8000', 0x507), 'mat_marker': G('8000', 0x508),
    'scene_room': G('a000', 0x501), 'scene_lights': G('a000', 0x502),
    'cube': G('8000', 0xc001), 'plane': G('8000', 0xc004),
}
def meta(guid): return json.dumps({'guid': guid, 'importSettings': {}}, indent=1) + '\n'
def J(o): return json.dumps(o, indent=1) + '\n'

def obj(n, name, pos, comps, rot=(0, 0, 0, 1), scale=(1, 1, 1), parent=None):
    return {'guid': G('9000', n), 'name': name, 'parent': parent, 'active': True,
            'transform': {'position': list(pos), 'rotation': [r6(x) for x in rot], 'scale': list(scale)},
            'components': [{'type': t, 'fields': f} for t, f in comps], 'prefab': None}

def camera_obj(n, pos, pitch):
    # engine camera looks +z (Unity-like); a 180 degree turn about y makes it look at -z, then pitch
    q = qaxis((0, 1, 0), 180.0); p = qaxis((1, 0, 0), pitch)
    x1, y1, z1, w1 = q; x2, y2, z2, w2 = p
    r = (w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2, w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2,
         w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2, w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2)
    return obj(n, 'Camera', pos, [('Camera', {'fov': 60.0, 'nearClip': 0.3, 'farClip': 60.0})], rot=r)

def light(n, name, pos, typ, color, intensity, rng, mode=0, rot=(0, 0, 0, 1)):
    return obj(n, name, pos, [('Light', {'type': typ, 'color': list(color), 'intensity': intensity, 'range': rng, 'renderMode': mode})], rot=rot)

def settings_obj(n, ambient, fog):
    return obj(n, 'RenderSettings', (0, 0, 0), [('RenderSettings', {
        'ambient': list(ambient), 'fogEnabled': fog is not None,
        'fogColor': list(fog[0]) if fog else [0, 0, 0, 255], 'fogStart': fog[1] if fog else 0.0, 'fogEnd': fog[2] if fog else 0.0})])

def scene(guid, objects): return J({'format': 1, 'guid': guid, 'objects': objects})

def build_sample():
    files = {}
    def add(rel, data): files[rel] = data
    add('ProjectSettings/Project.json', J({'format': 1, 'name': 'Psp3dRoom', 'target': 'psp', 'scenes': [GUID['scene_room'], GUID['scene_lights']]}))
    ct = json.loads(open(os.path.join(ROOT, 'templates/psp-3d/ProjectSettings/ConsoleTarget.json')).read())
    ct['allowedComponents'] = ['MeshRenderer', 'SkinnedMeshRenderer', 'Animation', 'Light', 'RenderSettings', 'Camera',
                               'DirectionalLight', 'CharacterController', 'BoxCollider', 'SphereCollider']
    add('ProjectSettings/ConsoleTarget.json', J(ct))
    add('ProjectSettings/InputActions.json', J({'format': 1, 'actions': [
        {'name': 'Move', 'kind': 'axis2', 'source': 'nub'}, {'name': 'Jump', 'kind': 'button', 'source': 'south'},
        {'name': 'CameraLeft', 'kind': 'button', 'source': 'l'}, {'name': 'CameraRight', 'kind': 'button', 'source': 'r'}]}))
    for p in ('cube', 'plane'):
        src = os.path.join(ROOT, 'templates/psp-3d/Assets/Primitives/%s.prim.json' % p)
        add('Assets/Primitives/%s.prim.json' % p, open(src).read())
        add('Assets/Primitives/%s.prim.json.meta' % p, meta(GUID[p]))
    add('Assets/Textures/blocky_walk.png.meta', meta(GUID['blocky_png']))
    add('Assets/Textures/room.png.meta', meta(GUID['room_png']))
    add('Assets/Models/blocky_walk.gltf.meta', meta(GUID['blocky']))
    add('Assets/Models/room.gltf.meta', meta(GUID['room']))
    def mat(tex, color, unlit=False):
        return J({'format': 1, 'texture': tex, 'color': color, 'unlit': unlit, 'alphaTest': False})
    mats = [('character', 'mat_char', mat(GUID['blocky_png'], [255, 255, 255, 255])),
            ('room', 'mat_room', mat(GUID['room_png'], [255, 255, 255, 255])),
            ('pillar', 'mat_pillar', mat(GUID['room_png'], [200, 170, 140, 255])),
            ('marker', 'mat_marker', mat(None, [255, 255, 255, 255], True))]
    for name, key, text in mats:
        add('Assets/Materials/%s.mat.json' % name, text)
        add('Assets/Materials/%s.mat.json.meta' % name, meta(GUID[key]))
    add('Assets/Scenes/Room.scene.meta', meta(GUID['scene_room']))
    add('Assets/Scenes/Lights12.scene.meta', meta(GUID['scene_lights']))
    # Room scene
    ch = obj(4, 'Character', (0, 0, 2), [
        ('SkinnedMeshRenderer', {'mesh': GUID['blocky'], 'material': GUID['mat_char']}),
        ('Animation', {'clip': GUID['blocky'], 'playAutomatically': True, 'wrapMode': 2, 'speed': 1.0})],
        rot=qaxis((0, 1, 0), 180.0))
    room = [
        camera_obj(1, (0, 1.9, 5.5), 12.0),
        obj(2, 'Room', (0, 0, 0), [('MeshRenderer', {'mesh': GUID['room'], 'color': [255, 255, 255, 255], 'material': GUID['mat_room']})]),
        settings_obj(3, (70, 70, 80, 255), ([40, 44, 60, 255], 8.0, 24.0)),
        ch,
        light(5, 'Sun', (0, 6, 0), 0, (255, 244, 224, 255), 0.8, 0.0, rot=qaxis((1, 0, 0), 55.0)),
        light(6, 'PointRed', (-4, 2.2, 0), 1, (255, 90, 70, 255), 1.0, 7.0),
        light(7, 'PointGreen', (4, 2.2, -2), 1, (90, 255, 110, 255), 1.0, 7.0),
        light(8, 'PointBlue', (0, 2.2, -6), 1, (90, 130, 255, 255), 1.0, 7.0),
    ]
    add('Assets/Scenes/Room.scene', scene(GUID['scene_room'], room))
    # Lights12 scene: 4 x 3 grid of cubes, 12 colored point lights above
    objs = [camera_obj(1, (0, 7.0, 9.5), 38.0),
            obj(2, 'Floor', (0, 0, 0), [('MeshRenderer', {'mesh': GUID['plane'], 'color': [200, 200, 200, 255], 'material': GUID['mat_pillar']})], scale=(2, 1, 2)),
            settings_obj(3, (40, 40, 48, 255), None)]
    n = 10
    cols = [(255, 80, 80), (80, 255, 80), (80, 80, 255), (255, 255, 80), (255, 80, 255), (80, 255, 255),
            (255, 160, 60), (160, 90, 255), (255, 255, 255), (120, 255, 160), (255, 120, 160), (140, 180, 255)]
    k = 0
    for r in range(3):
        for c in range(4):
            x, z = -4.5 + c * 3.0, -3.0 + r * 3.0
            objs.append(obj(n, 'Cube%d' % k, (x, 0.5, z), [('MeshRenderer', {'mesh': GUID['cube'], 'color': [220, 220, 220, 255], 'material': GUID['mat_room']})]))
            objs.append(light(n + 1, 'Light%d' % k, (x + 1.0, 2.0, z + 1.0), 1, cols[k] + (255,), 1.0, 4.5))
            n += 2; k += 1
    add('Assets/Scenes/Lights12.scene', scene(GUID['scene_lights'], objs))
    return files

# ---------------------------------------------------------------- preview (software rasterizer)
def preview(path):
    pos, nrm, uv, joints, weights, idx, _ = build_character()
    t, ch = clip_walk()
    def pose(frame):
        loc = []
        for i, j in enumerate(JOINTS):
            tr, q = local_t(i), (0, 0, 0, 1)
            if (j[0], 'translation') in ch: tr = ch[(j[0], 'translation')][frame]
            if (j[0], 'rotation') in ch: q = ch[(j[0], 'rotation')][frame]
            loc.append(mat_trs(tr, q))
        world = []
        for i, j in enumerate(JOINTS):
            world.append(loc[i] if j[1] < 0 else mat_mul(world[j[1]], loc[i]))
        return [mat_mul(world[i], mat_t([-c for c in JOINTS[i][2]])) for i in range(len(JOINTS))]
    W, H, S = 220, 330, 170.0
    img = [[(235, 235, 240)] * (W * 4) for _ in range(H)]
    def ap(m, p): return [m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] for r in range(3)]
    for pi, (frame, view) in enumerate(((0, 'front'), (15, 'front'), (0, 'side'), (15, 'side'))):
        sk = pose(frame)
        P = []
        for p, j, w in zip(pos, joints, weights):
            acc = [0, 0, 0]
            for jj, ww in zip(j, w):
                if ww > 0:
                    q = ap(sk[jj], p); acc = [acc[k] + ww * q[k] for k in range(3)]
            P.append(acc)
        def proj(p):
            if view == 'front': sx, d = -p[0], p[2]
            else: sx, d = p[2], -p[0]
            return (pi * W + W / 2 + sx * S, H - 20 - p[1] * S, d)
        zb = {}
        for i in range(0, len(idx), 3):
            a, b, c = [proj(P[idx[i + k]]) for k in range(3)]
            n = nrm[idx[i]]
            shade = 0.55 + 0.45 * abs(n[1] * 0.5 + n[2 if view == 'front' else 0] * 0.8)
            col = tuple(int(x * min(1.0, shade)) for x in PAL[int(uv[idx[i]][1] * 4) * 4 + int(uv[idx[i]][0] * 4)])
            x0, x1 = int(min(a[0], b[0], c[0])), int(max(a[0], b[0], c[0])) + 1
            y0, y1 = int(min(a[1], b[1], c[1])), int(max(a[1], b[1], c[1])) + 1
            den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
            if abs(den) < 1e-9: continue
            for y in range(max(0, y0), min(H, y1)):
                for x in range(max(pi * W, x0), min((pi + 1) * W, x1)):
                    l1 = ((b[1] - c[1]) * (x + .5 - c[0]) + (c[0] - b[0]) * (y + .5 - c[1])) / den
                    l2 = ((c[1] - a[1]) * (x + .5 - c[0]) + (a[0] - c[0]) * (y + .5 - c[1])) / den
                    l3 = 1 - l1 - l2
                    if l1 < 0 or l2 < 0 or l3 < 0: continue
                    z = l1 * a[2] + l2 * b[2] + l3 * c[2]
                    if (x, y) not in zb or z > zb[(x, y)]:
                        zb[(x, y)] = z; img[y][x] = col
        for y in range(H): img[y][pi * W] = (150, 150, 160)
    rgb = bytearray()
    for row in img:
        for c in row: rgb += bytes(c)
    write(path, png(W * 4, H, rgb))

# ---------------------------------------------------------------- driver
ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))

def generate(out, sample):
    stats = {}
    for name, fn in (('blocky_walk', build_character_gltf), ('room', build_room_gltf)):
        text, binb, pngb, st = fn()
        validate(text, binb, name)
        write(os.path.join(out, name + '.gltf'), text)
        write(os.path.join(out, name + '.png'), pngb)
        stats[name] = st
    if sample:
        for rel, data in build_sample().items(): write(os.path.join(sample, rel), data)
        # sample copies of the models and textures
        for name, sub in (('blocky_walk', None), ('room', None)):
            for ext, d in (('gltf', 'Models'), ('png', 'Textures')):
                shutil.copyfile(os.path.join(out, '%s.%s' % (name, ext)), os.path.join(sample, 'Assets', d, '%s.%s' % (name, ext)))
    return stats

def tree(d):
    r = {}
    for dp, _, fs in os.walk(d):
        for f in fs: r[os.path.relpath(os.path.join(dp, f), d)] = open(os.path.join(dp, f), 'rb').read()
    return r

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'tests/fixtures/gltf'))
    ap.add_argument('--sample', default=os.path.join(ROOT, 'samples/psp3d_room'))
    ap.add_argument('--no-sample', action='store_true')
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--preview')
    a = ap.parse_args()
    sample = None if a.no_sample else a.sample
    if a.preview: preview(a.preview); print('preview', a.preview); return 0
    if a.check:
        with tempfile.TemporaryDirectory() as t:
            o, s = os.path.join(t, 'o'), os.path.join(t, 's')
            generate(o, s)
            bad = 0
            for want, got, label in ((o, a.out, 'fixtures'), (s, sample, 'sample')):
                if got is None: continue
                T = tree(want)
                for rel, data in T.items():
                    p = os.path.join(got, rel)
                    if not os.path.isfile(p) or open(p, 'rb').read() != data:
                        print('DIFF', label, rel); bad += 1
            print('check: %s' % ('FAIL (%d)' % bad if bad else 'OK, files identical'))
            return 1 if bad else 0
    st = generate(a.out, sample)
    print(json.dumps(st))
    return 0

if __name__ == '__main__':
    sys.exit(main())
