#!/usr/bin/env python3
"""F6.07: regenerates samples/unity_playtest, the small game a (simulated) Unity developer built from docs/rensga-engine/guide-for-unity-users.md.

  python3 samples/unity_playtest/tools/make_sample.py [out dir]      (default: samples/unity_playtest)

Collect the coins before the time runs out: a data asset (GameTuning) holds the tuning, the coin is a prefab, the score is a UIText.
Generated: ProjectSettings, scenes Main (play with the nub) and MainBot (the bot plays; the AUTOSTART scene of the PSP run), the
GameTuning asset, Coin.prefab, flat-colour materials. Hand-written and left alone: Assets/Scripts/*.
"""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'samples'))
import phys_project as pp  # noqa: E402

SC_MAIN, SC_BOT = pp.G('a000', 0xd41), pp.G('a000', 0xd42)
TUNING, COIN = pp.G('d400', 1), pp.G('d400', 2)
ALLOWED = ['MeshRenderer', 'Camera', 'Light', 'RenderSettings', 'BoxCollider', 'CharacterController', 'UIText', 'CoinGame', 'Coin', 'PlayerMove']


def coin_prefab(mat_guid):
    cube = pp.PRIMS['cube'][1]
    return {'format': 1, 'guid': COIN, 'rootFileId': 1, 'nextFileId': 5, 'objects': [{
        'fileId': 1, 'name': 'Coin', 'parent': None, 'active': True, 'tag': 'Coin', 'layer': 0,
        'transform': {'position': [0, 0.6, 0], 'rotation': [0, 0, 0, 1], 'scale': [0.6, 0.6, 0.12]},
        'components': [
            {'fileId': 2, 'type': 'MeshRenderer', 'fields': {'mesh': cube, 'color': [255, 255, 255, 255], 'material': mat_guid}},
            {'fileId': 3, 'type': 'BoxCollider', 'fields': {'center': [0, 0, 0], 'size': [1.6, 1.6, 6], 'isTrigger': True}},
            {'fileId': 4, 'type': 'Coin', 'fields': {'spin': 180.0, 'value': 10}}]}]}


def build(bot):
    p = pp.Project('CoinRun')
    p.objs.append(pp.g.camera_obj(1, (0, 12.5, 9.5), 55.0))
    p.n = 1
    p.add('Sun', (0, 6, 0), [('Light', {'type': 0, 'color': [255, 244, 224, 255], 'intensity': 0.9, 'range': 0.0, 'renderMode': 0})], rot=pp.qaxis((1, 0, 0), 50.0))
    p.objs.append(pp.g.settings_obj(p.n + 1, (95, 95, 110, 255), None))
    p.n += 1
    p.box('Floor', (0, -0.5, 0), (16, 1, 16), (90, 120, 100, 255))
    for name, pos, size in (('WallN', (0, 0.5, -8.5), (17, 1, 1)), ('WallS', (0, 0.5, 8.5), (17, 1, 1)), ('WallW', (-8.5, 0.5, 0), (1, 1, 17)), ('WallE', (8.5, 0.5, 0), (1, 1, 17))):
        p.box(name, pos, size, (130, 110, 90, 255))
    mat = p.mat((255, 205, 40, 255))
    p.add('Player', (0, 1.0, 0), [
        ('MeshRenderer', {'mesh': pp.PRIMS['capsule'][1], 'color': [255, 255, 255, 255], 'material': p.mat((70, 120, 230, 255))}),
        ('CharacterController', {'height': 1.8, 'radius': 0.4}),
        ('PlayerMove', {'tuning': TUNING, 'bot': bot})])
    p.add('Game', (0, 0, 0), [('CoinGame', {'tuning': TUNING, 'coinPrefab': COIN, 'quitWhenDone': bot})])
    p.add('ScoreText', (0, 0, 0), [('UIText', {'anchor': 0, 'x': 8, 'y': 8, 'color': [255, 255, 255, 255], 'sortingOrder': 0, 'label': 'Score 0'})])
    return p, mat


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'samples', 'unity_playtest')
    files = {}
    mat = None
    for bot, guid, name in ((False, SC_MAIN, 'Main'), (True, SC_BOT, 'MainBot')):
        p, mat = build(bot)
        files.update(p.files)
        sj = p.scene_json(guid)
        if bot:  # object GUIDs must be unique across the scenes of one pack
            sj = sj.replace('-4000-9000-', '-4000-9001-')
        files['Assets/Scenes/%s.scene' % name] = sj
        files['Assets/Scenes/%s.scene.meta' % name] = pp.g.meta(guid)
    files['Assets/Prefabs/Coin.prefab'] = pp.J(coin_prefab(mat))
    files['Assets/Prefabs/Coin.prefab.meta'] = pp.J({'guid': COIN, 'importSettings': {}, 'importer': 'prefab'})
    files['Assets/Data/Tuning.asset.json'] = pp.J({'format': 1, 'kind': 'DataAsset', 'type': 'GameTuning',
        'fields': {'playerSpeed': 5.0, 'coinCount': 8, 'coinValue': 10, 'timeLimit': 30.0, 'arenaHalf': 6.0}})
    files['Assets/Data/Tuning.asset.json.meta'] = pp.J({'guid': TUNING, 'importSettings': {}, 'importer': 'dataasset'})
    for i, n in enumerate(('GameTuning.h', 'CoinGame.h', 'Coin.cpp', 'PlayerMove.cpp', 'CoinGame.cpp')):
        files['Assets/Scripts/%s.meta' % n] = pp.J({'guid': '00000000-0000-4000-8000-00000000d4%02x' % (i + 1), 'importSettings': {}, 'importer': 'script'})
    files['ProjectSettings/Project.json'] = pp.J({'format': 1, 'name': 'CoinRun', 'target': 'psp', 'scenes': [SC_MAIN, SC_BOT],
                                                  'componentReserve': {'MeshRenderer': 16, 'BoxCollider': 16, 'Coin': 16}})
    files['ProjectSettings/ConsoleTarget.json'] = pp.console_target(ALLOWED)
    files['ProjectSettings/InputActions.json'] = pp.input_actions()
    files['ProjectSettings/TagManager.json'] = pp.J({'format': 1, 'tags': ['Coin'], 'layers': json.loads(pp.tag_manager({}))['layers']})
    pp.write_all(out, files)
    print('unity_playtest written to', out)


main()
