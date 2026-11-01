#!/usr/bin/env python3
"""Regenerates samples/prefab_enemies (F6.01 prefab exercise project). Usage: python3 make_sample.py [out dir]

Writes ProjectSettings, a cube primitive, two materials, three prefabs (Enemy, a Boss variant of it, a Squad that nests two
Enemies) and the Arena scene with five Enemy instances (the third overrides its colour), a Boss and a Squad.
"""
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else HERE
ROOM = os.path.join(HERE, "..", "psp3d_room")

CUBE, PLANE = "00000000-0000-4000-8000-00000000c001", "00000000-0000-4000-8000-00000000c002"
MAT_ENEMY, MAT_FLOOR = "00000000-0000-4000-8000-0000000000e1", "00000000-0000-4000-8000-0000000000e2"
P_ENEMY, P_BOSS, P_SQUAD = "00000000-0000-4000-8000-00000000f001", "00000000-0000-4000-8000-00000000f002", "00000000-0000-4000-8000-00000000f003"
SCENE = "00000000-0000-4000-a000-00000000a001"


def write(rel, obj):
    path = os.path.join(OUT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        if isinstance(obj, str):
            f.write(obj)
        else:
            json.dump(obj, f, indent=1)
            f.write("\n")


def meta(rel, guid):
    write(rel + ".meta", {"guid": guid, "importSettings": {}})


def xf(p=(0, 0, 0), s=(1, 1, 1)):
    return {"position": list(p), "rotation": [0, 0, 0, 1], "scale": list(s)}


for sub in ("ProjectSettings",):
    os.makedirs(os.path.join(OUT, sub), exist_ok=True)
    for n in ("ConsoleTarget.json", "InputActions.json"):
        shutil.copyfile(os.path.join(ROOM, sub, n), os.path.join(OUT, sub, n))
write("ProjectSettings/Project.json", {"format": 1, "name": "PrefabEnemies", "target": "psp", "scenes": [SCENE]})

write("Assets/Primitives/cube.prim.json", '{"format":1,"shape":"cube","params":{"size":[1,1,1]}}\n')
meta("Assets/Primitives/cube.prim.json", CUBE)
write("Assets/Primitives/plane.prim.json", '{"format":1,"shape":"plane","params":{"size":[10,10],"cellSize":1}}\n')
meta("Assets/Primitives/plane.prim.json", PLANE)
write("Assets/Materials/enemy.mat.json", {"format": 1, "texture": None, "color": [255, 255, 255, 255], "unlit": False, "alphaTest": False})
meta("Assets/Materials/enemy.mat.json", MAT_ENEMY)
write("Assets/Materials/floor.mat.json", {"format": 1, "texture": None, "color": [90, 90, 100, 255], "unlit": False, "alphaTest": False})
meta("Assets/Materials/floor.mat.json", MAT_FLOOR)


def mesh(color):
    return {"mesh": CUBE, "color": color, "material": MAT_ENEMY}


# Enemy: object 1 (body) with component 2, child 3 (Eye) with component 4. Ids are never reused.
enemy = {"format": 1, "guid": P_ENEMY, "rootFileId": 1, "nextFileId": 5, "objects": [
    {"fileId": 1, "name": "Enemy", "parent": None, "transform": xf((0, 0.5, 0), (1, 1, 1)),
     "components": [{"fileId": 2, "type": "MeshRenderer", "fields": mesh([90, 190, 90, 255])}]},
    {"fileId": 3, "name": "Eye", "parent": 1, "transform": xf((0, 0.35, 0.5), (0.35, 0.35, 0.2)),
     "components": [{"fileId": 4, "type": "MeshRenderer", "fields": mesh([250, 250, 250, 255])}]}]}
write("Assets/Prefabs/Enemy.prefab", enemy)
meta("Assets/Prefabs/Enemy.prefab", P_ENEMY)

# Boss: a variant of Enemy with a purple body (object 1) and a bigger eye (object 3).
boss = {"format": 1, "guid": P_BOSS, "variant": {"base": P_ENEMY, "overrides": [
    {"fileId": 1, "path": "MeshRenderer.color", "value": [150, 60, 200, 255]},
    {"fileId": 3, "path": "@scale", "value": [0.6, 0.6, 0.3]}]}, "nextFileId": 1}
write("Assets/Prefabs/Boss.prefab", boss)
meta("Assets/Prefabs/Boss.prefab", P_BOSS)

# Squad: an empty root with two nested Enemies (objects 2 and 3); the second one has a blue body set inside the nested entry.
squad = {"format": 1, "guid": P_SQUAD, "rootFileId": 1, "nextFileId": 4, "objects": [
    {"fileId": 1, "name": "Squad", "parent": None, "transform": xf(), "components": []},
    {"fileId": 2, "name": "EnemyA", "parent": 1, "transform": xf((-1.5, 0.5, 0)), "components": [], "prefab": {"asset": P_ENEMY, "fileId": 1}},
    {"fileId": 3, "name": "EnemyB", "parent": 1, "transform": xf((1.5, 0.5, 0)), "components": [],
     "prefab": {"asset": P_ENEMY, "fileId": 1, "overrides": [{"fileId": 1, "path": "MeshRenderer.color", "value": [90, 120, 220, 255]}]}}]}
write("Assets/Prefabs/Squad.prefab", squad)
meta("Assets/Prefabs/Squad.prefab", P_SQUAD)


def obj(n, name, comps, p=(0, 0, 0), s=(1, 1, 1)):
    return {"guid": "00000000-0000-4000-9000-%012x" % n, "name": name, "parent": None, "active": True, "transform": xf(p, s),
            "components": comps, "prefab": None}


def instance(n, prefab, name, p, overrides=None):
    o = obj(n, name, [], p)
    o["prefab"] = {"asset": prefab, "fileId": 1}
    if overrides:
        o["overrides"] = overrides
    return o


objects = [
    obj(1, "Camera", [{"type": "Camera", "fields": {"fov": 60.0, "nearClip": 0.3, "farClip": 60.0}}], (0, 3.2, 7.5)),
    obj(2, "Sun", [{"type": "Light", "fields": {"type": 0, "color": [255, 244, 224, 255], "intensity": 1.0}}], (0, 6, 0)),
    obj(3, "Floor", [{"type": "MeshRenderer", "fields": {"mesh": PLANE, "color": [255, 255, 255, 255], "material": MAT_FLOOR}}], (0, 0, 0), (1.6, 1, 1.0)),
]
objects[0]["transform"]["rotation"] = [-0.2164, 0, 0, 0.9763]  # looks down about 25 degrees
objects[1]["transform"]["rotation"] = [0.7071, 0, 0, 0.7071]
for i in range(5):
    ov = [{"fileId": 1, "path": "MeshRenderer.color", "value": [220, 70, 60, 255]}] if i == 2 else None
    objects.append(instance(16 + i, P_ENEMY, "Enemy%d" % (i + 1), (-4 + 2 * i, 0, 1), ov))
objects.append(instance(30, P_BOSS, "Boss", (0, 0, -2.5)))
objects[-1]["transform"]["scale"] = [1.8, 1.8, 1.8]
objects.append(instance(31, P_SQUAD, "Squad", (-4.5, 0, -2.5)))
write("Assets/Scenes/Arena.scene", {"format": 1, "guid": SCENE, "objects": objects})
meta("Assets/Scenes/Arena.scene", SCENE)
