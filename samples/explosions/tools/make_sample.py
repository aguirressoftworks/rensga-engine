#!/usr/bin/env python3
"""F6.09: regenerates samples/explosions (click-spawned particle explosions with camera shake, a minimap camera, MaterialPropertyBlock
tints, a LineRenderer marker and Debug::DrawLine).

  python3 samples/explosions/tools/make_sample.py [out dir]      (default: samples/explosions)

Everything is generated here (cubes, flat colours, one procedural glow texture); Assets/Scripts/Explosions.cpp is the only hand-written
file and is left alone. Scenes:
  Explosions     manual: mouse click (desktop) or the nub + South (PSP) fires; the pointer is turned into a world point by the Camera API
  ExplosionsBot  a bot clicks at fixed screen points (the AUTOSTART scene of the PSP run and tools/ci/explosions.sh)
"""
import math, os, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'samples'))
import phys_project as pp  # noqa: E402

SC_PLAY, SC_BOT = pp.G('a000', 0x901), pp.G('a000', 0x902)
TEX_GLOW = pp.G('8000', 0xe001)
LAYER_NOMAP = 8
BOOMS = 6
ALLOWED = ['MeshRenderer', 'Camera', 'Light', 'RenderSettings', 'ParticleSystem', 'LineRenderer', 'ExplosionGame']


def glow_png(size=64):
    """RGBA PNG: a white disc with a soft (1 - r)^2 edge in the alpha channel."""
    rows = []
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            dx, dy = (x + 0.5) / size * 2 - 1, (y + 0.5) / size * 2 - 1
            r = math.sqrt(dx * dx + dy * dy)
            a = 0 if r >= 1 else int(round(255 * (1 - r) ** 2))
            row += bytes((255, 255, 255, a))
        rows.append(bytes(row))
    raw = b''.join(rows)

    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', size, size, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def ps(**kw):
    base = {'duration': 1.0, 'loop': False, 'playOnAwake': False, 'startLifetime': 1.0, 'startSpeed': 3.0, 'startSize': 1.0,
            'startColor': [255, 255, 255, 255], 'gravityModifier': 0.0, 'maxParticles': 40, 'seed': 1, 'emissionRate': 0.0,
            'burstTimes': [], 'burstCounts': [], 'shape': 1, 'shapeRadius': 0.3, 'shapeAngle': 25.0, 'colorOverLifetime': True,
            'colorStart': [255, 255, 255, 255], 'colorEnd': [255, 255, 255, 0], 'sizeOverLifetime': True, 'sizeStart': 1.0, 'sizeEnd': 1.0,
            'texture': TEX_GLOW}
    base.update(kw)
    return ('ParticleSystem', base)


def build(bot):
    p = pp.Project('Explosions')
    cam = p.add('Camera', (0, 9.5, 15.5), [('Camera', {'fov': 60.0, 'nearClip': 0.3, 'farClip': 80.0, 'depth': 0.0})], rot=pp.qaxis((1, 0, 0), -30.0))
    mini = p.add('MiniMap', (0, 30, 0), [('Camera', {'fov': 60.0, 'nearClip': 0.3, 'farClip': 80.0, 'depth': 1.0, 'orthographic': True,
                                                   'orthographicSize': 11.0, 'viewportX': 0.74, 'viewportY': 0.60, 'viewportW': 0.25,
                                                   'viewportH': 0.38, 'clearFlags': 1, 'backgroundColor': [16, 20, 26, 255],
                                                   'cullingMask': -1 - (1 << LAYER_NOMAP)})], rot=pp.qaxis((1, 0, 0), -90.0))
    p.add('Sun', (0, 6, 0), [('Light', {'type': 0, 'color': [255, 240, 220, 255], 'intensity': 0.55, 'range': 0.0, 'renderMode': 0})],
          rot=pp.qaxis((1, 0, 0), 125.0))  # lights shine along +Z: 125 degrees about X = down and toward -Z, onto the faces that face the camera
    p.add('BoomLight', (0, 1.5, 0), [('Light', {'type': 1, 'color': [255, 170, 80, 255], 'intensity': 0.0, 'range': 9.0, 'renderMode': 1})])
    p.objs.append(pp.g.settings_obj(p.n + 1, (70, 74, 92, 255), None))
    p.n += 1
    p.box('Ground', (0, -0.5, 0), (44, 1, 44), (96, 112, 84, 255))
    p.box('Backdrop', (0, 2.5, -13), (36, 5, 1), (118, 104, 96, 255))
    for i, (x, z) in enumerate(((-7, -5), (7, -5), (-9, 1), (9, 1))):
        p.box('Pillar%d' % i, (x, 1.5, z), (1.2, 3, 1.2), (150, 128, 104, 255))
    crates = ((-3.5, 0.5, 3.0), (3.5, 0.5, 2.0), (0.0, 0.5, -3.5), (-5.5, 0.5, -1.5), (5.5, 0.5, -2.0))
    for i, (x, y, z) in enumerate(crates):  # one shared material: the script tints them one by one with a MaterialPropertyBlock
        p.box('Crate%d' % i, (x, y, z), (1, 1, 1), (182, 150, 98, 255))
    p.box('Tower', (11.5, 3, -6), (2.2, 6, 2.2), (206, 84, 72, 255), layer=LAYER_NOMAP)  # hidden from the minimap by its culling mask
    for i in range(BOOMS):
        base = (i * 3.0 - 7.5, -20, 0)  # parked far below the ground until used
        p.add('Fire%d' % i, base, [ps(startLifetime=0.55, startSpeed=4.6, startSize=2.8, shape=1, shapeRadius=0.35, maxParticles=40,
                                      startColor=[255, 190, 80, 255], colorStart=[255, 240, 190, 255], colorEnd=[210, 40, 10, 0],
                                      sizeStart=0.7, sizeEnd=1.5, seed=11 + i)])
        p.add('Smoke%d' % i, base, [ps(startLifetime=1.5, startSpeed=1.9, startSize=3.4, shape=2, shapeRadius=0.5, maxParticles=36,
                                       gravityModifier=-0.08, startColor=[110, 108, 104, 210], colorStart=[255, 255, 255, 255],
                                       colorEnd=[255, 255, 255, 0], sizeStart=0.5, sizeEnd=1.6, seed=31 + i)])
        p.add('Sparks%d' % i, base, [ps(startLifetime=1.0, startSpeed=8.5, startSize=0.45, shape=0, shapeRadius=0.1, shapeAngle=38.0,
                                        maxParticles=48, gravityModifier=1.0, startColor=[255, 236, 140, 255],
                                        colorStart=[255, 255, 255, 255], colorEnd=[255, 110, 20, 0], sizeStart=1.0, sizeEnd=0.4,
                                        seed=51 + i)])
    # a ring that follows the last explosion, drawn by a LineRenderer (8 points, closed)
    ring = [[round(1.4 * math.cos(a * math.pi / 4), 4), 0.05, round(1.4 * math.sin(a * math.pi / 4), 4)] for a in range(8)]
    p.add('Marker', (0, -20, 0), [('LineRenderer', {'positions': ring, 'width': 0.12, 'color': [255, 232, 120, 255], 'useWorldSpace': False,
                                                  'loop': True})])
    p.add('Game', (0, 0, 0), [('ExplosionGame', {'bot': bot, 'interval': 36, 'maxBooms': 8, 'shake': 0.5, 'booms': BOOMS, 'crates': len(crates)})])
    for o in p.objs:  # visuals only: the box helper adds colliders, this sample has no physics
        o['components'] = [c for c in o['components'] if c['type'] != 'BoxCollider']
    return p


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'samples', 'explosions')
    files = {}
    for bot, guid, name in ((False, SC_PLAY, 'Explosions'), (True, SC_BOT, 'ExplosionsBot')):
        p = build(bot)
        files.update(p.files)
        sj = p.scene_json(guid)
        if bot:  # object GUIDs must be unique across the scenes of one pack
            sj = sj.replace('-4000-9000-', '-4000-9001-')
        files['Assets/Scenes/%s.scene' % name] = sj
        files['Assets/Scenes/%s.scene.meta' % name] = pp.g.meta(guid)
    files['Assets/Textures/glow.png'] = glow_png()
    files['Assets/Textures/glow.png.meta'] = pp.J({'guid': TEX_GLOW, 'importSettings': {}, 'targetOverrides': {'psp': {'format': 'rgba4444'}}})
    files['Assets/Scripts/Explosions.cpp.meta'] = pp.J({'guid': '00000000-0000-4000-8000-00000000e901', 'importSettings': {}, 'importer': 'script'})
    files['ProjectSettings/Project.json'] = pp.J({'format': 1, 'name': 'Explosions', 'target': 'psp', 'scenes': [SC_PLAY, SC_BOT]})
    files['ProjectSettings/ConsoleTarget.json'] = pp.console_target(ALLOWED)
    files['ProjectSettings/InputActions.json'] = pp.input_actions()
    files['ProjectSettings/TagManager.json'] = pp.tag_manager({LAYER_NOMAP: 'NoMap'})
    files['ProjectSettings/Physics.json'] = pp.J({'format': 1, 'gravity': [0, -9.81, 0], 'ignore': [], 'queriesHitTriggers': True})
    pp.write_all(out, files)
    print('explosions written to', out)


main()
