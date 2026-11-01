#!/usr/bin/env python3
"""F6.07: regenerates samples/extending_editor (editor extensions + data assets).

  python3 samples/extending_editor/tools/make_sample.py [out dir]      (default: samples/extending_editor)

Generated: ProjectSettings, the Arena scene (ground, camera, sun, three Patroller cubes), the three EnemyStats data assets and the
flat-colour materials. Hand-written and left alone: Assets/Scripts/*.{h,cpp} and Editor/*.cpp.
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'samples'))
import phys_project as pp  # noqa: E402

SC_ARENA = pp.G('a000', 0xd31)
ALLOWED = ['MeshRenderer', 'Camera', 'Light', 'RenderSettings', 'BoxCollider', 'Patroller']

# (file stem, guid, fields) - the same JSON the Inspector writes (every field, declaration order)
STATS = [
    ('Grunt', pp.G('d300', 1), {'label': 'Grunt', 'kind': 0, 'health': 30, 'speed': 2.0, 'tint': [220, 80, 60, 255], 'pauses': [0.5]}),
    ('Scout', pp.G('d300', 2), {'label': 'Scout', 'kind': 1, 'health': 18, 'speed': 5.5, 'tint': [80, 170, 240, 255], 'pauses': [0.1]}),
    ('Brute', pp.G('d300', 3), {'label': 'Brute', 'kind': 2, 'health': 120, 'speed': 0.9, 'tint': [150, 110, 220, 255], 'pauses': [1.5]}),
]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'samples', 'extending_editor')
    p = pp.Project('ExtendingEditor')
    p.objs.append(pp.g.camera_obj(1, (0, 7.5, 9.5), 40.0))
    p.n = 1
    p.add('Sun', (0, 6, 0), [('Light', {'type': 0, 'color': [255, 244, 224, 255], 'intensity': 0.9, 'range': 0.0, 'renderMode': 0})], rot=pp.qaxis((1, 0, 0), 55.0))
    p.objs.append(pp.g.settings_obj(p.n + 1, (95, 95, 110, 255), None))
    p.n += 1
    p.box('Floor', (0, -0.5, 0), (16, 1, 12), (96, 124, 96, 255))
    for i, (name, guid, fields) in enumerate(STATS):
        z = -3.0 + 3.0 * i
        p.box('Patrol' + name, (-3, 0.5, z), (1, 1, 1), (200, 200, 200, 255),
              extra=[('Patroller', {'stats': guid, 'pointA': [-3, 0.5, z], 'pointB': [3, 0.5, z], 'stopAfterFrames': 480})])
    files = dict(p.files)
    files['Assets/Scenes/Arena.scene'] = p.scene_json(SC_ARENA)
    files['Assets/Scenes/Arena.scene.meta'] = pp.g.meta(SC_ARENA)
    for name, guid, fields in STATS:
        files['Assets/Data/%s.asset.json' % name] = pp.J({'format': 1, 'kind': 'DataAsset', 'type': 'EnemyStats', 'fields': fields})
        files['Assets/Data/%s.asset.json.meta' % name] = pp.J({'guid': guid, 'importSettings': {}, 'importer': 'dataasset'})
    files['Assets/Scripts/EnemyStats.h.meta'] = pp.J({'guid': '00000000-0000-4000-8000-00000000d301', 'importSettings': {}, 'importer': 'script'})
    files['Assets/Scripts/Patroller.cpp.meta'] = pp.J({'guid': '00000000-0000-4000-8000-00000000d302', 'importSettings': {}, 'importer': 'script'})
    files['ProjectSettings/Project.json'] = pp.J({'format': 1, 'name': 'ExtendingEditor', 'target': 'psp', 'scenes': [SC_ARENA]})
    files['ProjectSettings/ConsoleTarget.json'] = pp.console_target(ALLOWED)
    files['ProjectSettings/InputActions.json'] = pp.input_actions()
    files['ProjectSettings/TagManager.json'] = pp.tag_manager({})
    pp.write_all(out, files)
    print('extending_editor written to', out)


main()
