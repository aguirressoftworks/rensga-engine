#!/usr/bin/env python3
"""F6.08: writes the `determinism.sh phys` project (a 3D physics scene) to a directory.

  python3 tests/determinism/make_phys_project.py <out dir>

The scene: a floor, an oriented ramp, a capsule post, crates (boxes), spheres and a capsule falling onto them as Rigidbodies, a kinematic
platform moved with MovePosition, a CharacterController walking up the ramp, ghost boxes on an ignored layer, and a probe script whose
raycasts / spherecasts / overlaps move two markers. Linux rensga_headless and the PSP simulation ELF must reach the same state hash on every
frame.
"""
import os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'samples'))
import phys_project as pp  # noqa: E402

SCENE = pp.G('a000', 0x701)
ALLOWED = ['MeshRenderer', 'Camera', 'Light', 'RenderSettings', 'BoxCollider', 'SphereCollider', 'CapsuleCollider', 'Rigidbody',
           'CharacterController', 'PhysMover', 'PhysHero', 'PhysProbe']


def build():
    p = pp.Project('PhysDet')
    p.objs.append(pp.g.camera_obj(1, (0, 7, 16), 12.0))
    p.n = 1
    p.add('Sun', (0, 6, 0), [('Light', {'type': 0, 'color': [255, 244, 224, 255], 'intensity': 0.8, 'range': 0.0, 'renderMode': 0})], rot=pp.qaxis((1, 0, 0), 55.0))
    p.objs.append(pp.g.settings_obj(p.n + 1, (90, 90, 100, 255), None))
    p.n += 1
    p.box('Floor', (0, -0.5, 0), (40, 1, 24), (120, 130, 120, 255))
    p.box('Ramp', (-2.5, 1.0, 0), (9, 0.5, 5), (180, 150, 110, 255), rot=pp.qaxis((0, 0, 1), -20.0))
    p.box('RampRight', (6.2, 0.35, 0), (3.0, 0.4, 5), (160, 140, 120, 255), rot=pp.qaxis((0, 0, 1), 12.0))
    p.capsule('Post', (7.5, 1.0, 3.0), (200, 90, 80, 255))
    p.box('Platform', (-9, 0.2, -3), (3, 0.4, 3), (90, 140, 200, 255), body={'isKinematic': True, 'useGravity': False},
          extra=[('PhysMover', {'range': 4.0, 'period': 180})])
    crate_col = (210, 170, 70, 255)
    for i in range(6):
        p.box('Crate%d' % i, (-4.0 + 0.15 * (i % 3), 4.0 + 1.25 * i, 0.3 * (i % 2)), (1, 1, 1), crate_col, layer=8,
              body={'mass': 1.0 + 0.5 * (i % 2)})
    for i in range(3):
        p.sphere('Ball%d' % i, (1.5 + 1.1 * i, 7.0 + 0.5 * i, -0.4 * i), 0.5, (90, 190, 120, 255), layer=8, body={'mass': 0.8})
    p.capsule('Pill', (4.0, 8.0, 1.0), (150, 110, 200, 255), layer=8, body={'mass': 1.2})
    p.box('Barrel', (-7.0, 6.0, 2.0), (0.8, 1.2, 0.8), (200, 120, 60, 255), layer=8, body={'mass': 2.0, 'constraints': 56})
    for i in range(2):  # ghost boxes on layer 9: they ignore layer 8 (Physics.json) and land on the floor through the crates
        p.box('Ghost%d' % i, (-4.0 + 0.5 * i, 14.0 + 2 * i, 0.1), (1.2, 1.2, 1.2), (230, 230, 240, 255), layer=9, body={'mass': 1.0})
    p.box('Zone', (2.0, 1.0, 2.5), (3, 2, 2), (255, 255, 255, 255), trigger=True, visible=False)
    p.add('Hero', (3.0, 1.0, 1.5), [
        ('MeshRenderer', {'mesh': pp.PRIMS['capsule'][1], 'color': [255, 255, 255, 255], 'material': p.mat((70, 110, 220, 255))}),
        ('CharacterController', {'height': 1.8, 'radius': 0.4, 'gravity': -20.0, 'stepOffset': 0.3, 'slopeLimit': 45.0}),
        ('PhysHero', {'speed': 0.045})])
    p.add('Marker', (0, 3, 0), [('MeshRenderer', {'mesh': pp.PRIMS['sphere'][1], 'color': [255, 255, 255, 255], 'material': p.mat((255, 60, 60, 255), True)})], scale=(0.3, 0.3, 0.3))
    p.add('Marker2', (0, 3, -1), [('MeshRenderer', {'mesh': pp.PRIMS['sphere'][1], 'color': [255, 255, 255, 255], 'material': p.mat((60, 255, 60, 255), True)})], scale=(0.3, 0.3, 0.3))
    p.add('Probe', (0, 0, 0), [('PhysProbe', {})])
    return p


def main():
    out = sys.argv[1]
    p = build()
    files = dict(p.files)
    files['Assets/Scenes/Phys.scene'] = p.scene_json(SCENE)
    files['Assets/Scenes/Phys.scene.meta'] = pp.g.meta(SCENE)
    files['Assets/Scripts/PhysDet.cpp.meta'] = pp.J({'guid': '00000000-0000-4000-8000-00000000a701', 'importSettings': {}, 'importer': 'script'})
    files['ProjectSettings/Project.json'] = pp.J({'format': 1, 'name': 'PhysDet', 'target': 'psp', 'scenes': [SCENE]})
    files['ProjectSettings/ConsoleTarget.json'] = pp.console_target(ALLOWED)
    files['ProjectSettings/InputActions.json'] = pp.input_actions()
    files['ProjectSettings/TagManager.json'] = pp.tag_manager({8: 'Crates', 9: 'Ghosts'})
    files['ProjectSettings/Physics.json'] = pp.J({'format': 1, 'gravity': [0, -9.81, 0], 'ignore': [[8, 9]], 'queriesHitTriggers': True})
    shutil.rmtree(os.path.join(out, 'Assets'), ignore_errors=True)
    shutil.rmtree(os.path.join(out, 'ProjectSettings'), ignore_errors=True)
    pp.write_all(out, files)
    shutil.copyfile(os.path.join(HERE, 'PhysDet.cpp'), os.path.join(out, 'Assets/Scripts/PhysDet.cpp'))
    print('phys determinism project written to', out)


main()
