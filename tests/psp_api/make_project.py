#!/usr/bin/env python3
"""F5.18: writes the API test project (a small game project whose only script is ApiTests.cpp) to a directory.

  python3 tests/psp_api/make_project.py <out dir>

Assets (models, textures, materials) are copied from samples/psp3d_walk; nothing is stored twice in the repository. Scenes:
  ApiTests      the checks (all must pass on every profile)
  ApiTestsFail  the same plus one check that is wrong on the PSP only (failDemo), used to show that the script catches a PSP-only failure
"""
import json, os, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'samples'))
import gen_blocky as g  # noqa: E402

SC_OK, SC_FAIL = g.G('a000', 0x601), g.G('a000', 0x602)

def scene_objs(fail):
    ch = g.obj(4, 'Character', (0, 0, 2), [
        ('SkinnedMeshRenderer', {'mesh': g.GUID['blocky'], 'material': g.GUID['mat_char']}),
        ('Animation', {'clip': g.GUID['blocky'], 'playAutomatically': False, 'wrapMode': 2, 'speed': 1.0})],
        rot=g.qaxis((0, 1, 0), 180.0))
    return [
        g.camera_obj(1, (0, 1.9, 5.5), 12.0),
        g.obj(2, 'Room', (0, 0, 0), [('MeshRenderer', {'mesh': g.GUID['room'], 'color': [255, 255, 255, 255], 'material': g.GUID['mat_room']})]),
        g.settings_obj(3, (70, 70, 80, 255), ([40, 44, 60, 255], 8.0, 24.0)),
        ch,
        g.light(5, 'Sun', (0, 6, 0), 0, (255, 244, 224, 255), 0.8, 0.0, rot=g.qaxis((1, 0, 0), 55.0)),
        g.light(6, 'PointRed', (-4, 2.2, 0), 1, (255, 90, 70, 255), 1.0, 7.0),
        g.obj(7, 'SfxImportant', (0, 0, 0), [('AudioSource', {'priority': 40})]),
        g.obj(8, 'SfxDefault', (0, 0, 0), [('AudioSource', {})]),
        g.obj(9, 'Tests', (0, 0, 0), [('ApiTests', {'failDemo': fail})]),
    ]

def main():
    out = sys.argv[1]
    files = {}
    walk = g.build_sample(True)
    for rel, data in walk.items():
        if rel.startswith(('Assets/Materials', 'Assets/Primitives', 'ProjectSettings/InputActions')) or rel.endswith(('.gltf.meta', '.png.meta')):
            files[rel] = data
    ct = json.loads(walk['ProjectSettings/ConsoleTarget.json'])
    ct['allowedComponents'] = [c for c in ct['allowedComponents'] if c != 'RoomRig'] + ['AudioSource', 'ApiTests']
    files['ProjectSettings/ConsoleTarget.json'] = g.J(ct)
    files['ProjectSettings/Project.json'] = g.J({'format': 1, 'name': 'PspApiTests', 'target': 'psp', 'scenes': [SC_OK, SC_FAIL]})
    files['Assets/Scenes/ApiTests.scene'] = g.scene(SC_OK, scene_objs(False))
    files['Assets/Scenes/ApiTests.scene.meta'] = g.meta(SC_OK)
    files['Assets/Scenes/ApiTestsFail.scene'] = g.scene(SC_FAIL, [dict(o, guid=g.G('9000', 0x100 + int(o['guid'].split('-')[-1], 16))) for o in scene_objs(True)])
    files['Assets/Scenes/ApiTestsFail.scene.meta'] = g.meta(SC_FAIL)
    files['Assets/Scripts/ApiTests.cpp.meta'] = g.J({'guid': '00000000-0000-4000-8000-00000000a501', 'importSettings': {}, 'importer': 'script'})
    shutil.rmtree(os.path.join(out, 'Assets'), ignore_errors=True)
    for rel, data in files.items():
        g.write(os.path.join(out, rel), data)
    for name in ('blocky_walk.gltf', 'room.gltf'):
        shutil.copyfile(os.path.join(ROOT, 'samples/psp3d_walk/Assets/Models', name), os.path.join(out, 'Assets/Models', name))
    for name in ('blocky_walk.png', 'room.png'):
        shutil.copyfile(os.path.join(ROOT, 'samples/psp3d_walk/Assets/Textures', name), os.path.join(out, 'Assets/Textures', name))
    shutil.copyfile(os.path.join(HERE, 'ApiTests.cpp'), os.path.join(out, 'Assets/Scripts/ApiTests.cpp'))
    print('psp_api project written to', out)

main()
