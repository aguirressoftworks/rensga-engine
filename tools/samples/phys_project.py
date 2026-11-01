"""F6.08: tiny helpers to write a 3D physics project (primitives, flat-colour materials, a scene of colliders) used by
tests/determinism/make_phys_project.py (the `determinism.sh phys` fixture) and samples/shooting_range/tools/make_sample.py.

Everything is generated: no assets are copied or ripped. Objects are cubes, spheres and capsules (primitive meshes) with unlit or lit
flat-colour materials; colliders are added by the scene builders.
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_blocky as g  # noqa: E402  (G, J, meta, write, obj, qaxis, camera_obj, light, settings_obj, scene)

G = g.G
J = g.J
qaxis = g.qaxis

# primitive meshes: unit cube, sphere radius 0.5, capsule radius 0.4 height 1.8 (matches the colliders below)
PRIMS = {
    'cube': ({'format': 1, 'shape': 'cube', 'params': {'size': [1, 1, 1]}}, G('8000', 0xc001)),
    'sphere': ({'format': 1, 'shape': 'sphere', 'params': {'radius': 0.5}}, G('8000', 0xc002)),
    'capsule': ({'format': 1, 'shape': 'capsule', 'params': {'radius': 0.4, 'height': 1.8}}, G('8000', 0xc003)),
}


def material(n, color, unlit=False):
    return ({'format': 1, 'texture': None, 'color': list(color), 'unlit': unlit, 'alphaTest': False}, G('8000', 0x600 + n))


class Project:
    def __init__(self, name):
        self.name = name
        self.files = {}
        self.mats = {}      # colour tuple -> guid
        self.objs = []
        self.n = 0
        for key, (data, guid) in PRIMS.items():
            self.files['Assets/Primitives/%s.prim.json' % key] = json.dumps(data, separators=(',', ':')) + '\n'
            self.files['Assets/Primitives/%s.prim.json.meta' % key] = g.meta(guid)

    def mat(self, color, unlit=False):
        key = (tuple(color), unlit)
        if key not in self.mats:
            data, guid = material(len(self.mats), color, unlit)
            self.mats[key] = guid
            self.files['Assets/Materials/m%d.mat.json' % (len(self.mats) - 1)] = J(data)
            self.files['Assets/Materials/m%d.mat.json.meta' % (len(self.mats) - 1)] = g.meta(guid)
        return self.mats[key]

    def add(self, name, pos, comps, rot=(0, 0, 0, 1), scale=(1, 1, 1), layer=0, tag='', parent=None):
        self.n += 1
        o = g.obj(self.n, name, pos, comps, rot=rot, scale=scale, parent=parent)
        if layer:
            o['layer'] = layer
        if tag:
            o['tag'] = tag
        self.objs.append(o)
        return o

    # meshes: the visual is the same object as the collider (scale = size)
    def box(self, name, pos, size, color, rot=(0, 0, 0, 1), layer=0, body=None, trigger=False, unlit=False, extra=(), visible=True, tag=''):
        comps = []
        if visible:
            comps.append(('MeshRenderer', {'mesh': PRIMS['cube'][1], 'color': [255, 255, 255, 255], 'material': self.mat(color, unlit)}))
        comps.append(('BoxCollider', {'center': [0, 0, 0], 'size': [1, 1, 1], 'isTrigger': trigger}))
        if body is not None:
            comps.append(('Rigidbody', body))
        comps += list(extra)
        return self.add(name, pos, comps, rot=rot, scale=size, layer=layer, tag=tag)

    def sphere(self, name, pos, radius, color, layer=0, body=None, trigger=False, unlit=False, extra=(), tag=''):
        comps = [('MeshRenderer', {'mesh': PRIMS['sphere'][1], 'color': [255, 255, 255, 255], 'material': self.mat(color, unlit)}),
                 ('SphereCollider', {'radius': 0.5, 'isTrigger': trigger})]
        if body is not None:
            comps.append(('Rigidbody', body))
        comps += list(extra)
        return self.add(name, pos, comps, scale=(radius * 2,) * 3, layer=layer, tag=tag)

    def capsule(self, name, pos, color, layer=0, body=None, rot=(0, 0, 0, 1), unlit=False, extra=()):
        comps = [('MeshRenderer', {'mesh': PRIMS['capsule'][1], 'color': [255, 255, 255, 255], 'material': self.mat(color, unlit)}),
                 ('CapsuleCollider', {'center': [0, 0, 0], 'radius': 0.4, 'height': 1.8, 'direction': 1, 'isTrigger': False})]
        if body is not None:
            comps.append(('Rigidbody', body))
        comps += list(extra)
        return self.add(name, pos, comps, rot=rot, layer=layer)

    def scene_json(self, guid):
        return g.scene(guid, self.objs)


def console_target(allowed):
    ct = json.load(open(os.path.join(HERE, '..', '..', 'samples', 'psp3d_walk', 'ProjectSettings', 'ConsoleTarget.json')))
    ct['allowedComponents'] = list(allowed)
    return J(ct)


def input_actions():
    return open(os.path.join(HERE, '..', '..', 'samples', 'psp3d_walk', 'ProjectSettings', 'InputActions.json')).read()


def tag_manager(layer_names):
    layers = ['Default', 'TransparentFX', 'Ignore Raycast', '', 'Water', 'UI'] + [''] * 26
    for i, n in layer_names.items():
        layers[i] = n
    return J({'format': 1, 'tags': [], 'layers': layers})


def write_all(out, files):
    for rel, data in files.items():
        g.write(os.path.join(out, rel), data)
