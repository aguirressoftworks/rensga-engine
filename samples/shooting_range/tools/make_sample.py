#!/usr/bin/env python3
"""F6.08: regenerates samples/shooting_range (a raycast shooting range with layers and trigger zones).

  python3 samples/shooting_range/tools/make_sample.py [out dir]      (default: samples/shooting_range)

Everything is generated here; Assets/Scripts/Range.cpp is the only hand-written file and is left alone. Scenes:
  Range     manual play: the nub aims, South fires
  RangeBot  the bot plays (RangeGame.bot = true): the AUTOSTART scene of the PSP runs and tools/ci/physics_range.sh
"""
import os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'samples'))
import phys_project as pp  # noqa: E402

SC_RANGE, SC_BOT = pp.G('a000', 0x801), pp.G('a000', 0x802)
LAYER_TARGET, LAYER_IGNORE, LAYER_ZONE, LAYER_RUNNER = 8, 9, 10, 11
ALLOWED = ['MeshRenderer', 'Camera', 'Light', 'RenderSettings', 'BoxCollider', 'SphereCollider', 'CapsuleCollider', 'Rigidbody',
           'RangeGame', 'ZoneCounter']

# targets: (x, z); the decoys stand on the line of sight from the muzzle (0, 1.6, 9) to targets 1 and 3
TARGETS = [(-5.0, -3.0), (-2.0, -6.0), (1.5, -9.0), (5.0, -5.0), (-6.0, -11.0)]
DECOYS = [('Decoy0', (-1.67, 0.8, -3.5)), ('Decoy1', (3.57, 0.8, -1.0))]


def build(bot):
    p = pp.Project('ShootingRange')
    p.objs.append(pp.g.camera_obj(1, (0, 4.4, 14.5), 14.0))
    p.n = 1
    p.add('Sun', (0, 6, 0), [('Light', {'type': 0, 'color': [255, 244, 224, 255], 'intensity': 0.85, 'range': 0.0, 'renderMode': 0})], rot=pp.qaxis((1, 0, 0), 50.0))
    p.objs.append(pp.g.settings_obj(p.n + 1, (95, 95, 110, 255), None))
    p.n += 1
    p.add('Game', (0, 0, 0), [('RangeGame', {'bot': bot, 'shotInterval': 24, 'maxShots': 14, 'knockback': 6.0, 'targetCount': len(TARGETS)})])
    p.box('Floor', (0, -0.5, 0), (30, 1, 40), (96, 124, 96, 255))
    p.box('Backstop', (0, 3.0, -16.5), (28, 6, 1), (140, 120, 100, 255))
    p.box('LaneL', (-14.5, 0.5, -2.0), (1, 1, 36), (120, 100, 90, 255))
    p.box('LaneR', (14.5, 0.5, -2.0), (1, 1, 36), (120, 100, 90, 255))
    p.box('Bench', (0, 0.45, 9.8), (4, 0.9, 1), (110, 80, 60, 255))
    for i, (x, z) in enumerate(TARGETS):
        p.box('Target%d' % i, (x, 0.7, z), (0.9, 1.4, 0.3), (225, 70, 60, 255), layer=LAYER_TARGET, body={'mass': 1.5})
    for name, pos in DECOYS:
        p.box(name, pos, (1.3, 1.6, 0.3), (70, 110, 220, 255), layer=LAYER_IGNORE)
    p.box('Zone', (0, 1.0, 7.0), (3, 2, 2.4), (255, 255, 255, 255), layer=LAYER_ZONE, trigger=True, visible=False, extra=[('ZoneCounter', {})])
    p.sphere('Runner', (-5.0, 0.5, 7.0), 0.5, (240, 200, 60, 255), layer=LAYER_RUNNER, body={'mass': 1.0})
    # zone floor markings (visual only)
    p.box('ZonePad', (0, 0.01, 7.0), (3, 0.02, 2.4), (255, 230, 120, 255), visible=True)
    objs_clean = p.objs
    for o in objs_clean:  # the pad is a visual only: drop its collider
        if o['name'] == 'ZonePad':
            o['components'] = [c for c in o['components'] if c['type'] != 'BoxCollider']
    return p


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'samples', 'shooting_range')
    files = {}
    for bot, guid, name in ((False, SC_RANGE, 'Range'), (True, SC_BOT, 'RangeBot')):
        p = build(bot)
        files.update(p.files)
        sj = p.scene_json(guid)
        if bot:  # object GUIDs must be unique across the scenes of one pack
            sj = sj.replace('-4000-9000-', '-4000-9001-')
        files['Assets/Scenes/%s.scene' % name] = sj
        files['Assets/Scenes/%s.scene.meta' % name] = pp.g.meta(guid)
    files['Assets/Scripts/Range.cpp.meta'] = pp.J({'guid': '00000000-0000-4000-8000-00000000a801', 'importSettings': {}, 'importer': 'script'})
    files['ProjectSettings/Project.json'] = pp.J({'format': 1, 'name': 'ShootingRange', 'target': 'psp', 'scenes': [SC_RANGE, SC_BOT]})
    files['ProjectSettings/ConsoleTarget.json'] = pp.console_target(ALLOWED)
    files['ProjectSettings/InputActions.json'] = pp.input_actions()
    files['ProjectSettings/TagManager.json'] = pp.tag_manager({LAYER_TARGET: 'Target', LAYER_IGNORE: 'Ignore', LAYER_ZONE: 'Zone', LAYER_RUNNER: 'Runner'})
    # The decoys' layer does not collide with Targets (a body on one passes through the other); shots skip the Ignore layer through the LayerMask.
    files['ProjectSettings/Physics.json'] = pp.J({'format': 1, 'gravity': [0, -9.81, 0], 'ignore': [['Ignore', 'Target']], 'queriesHitTriggers': True})
    pp.write_all(out, files)
    print('shooting_range written to', out)


main()
