#!/usr/bin/env python3
"""Regenerates samples/pool_spawner: the two scenes, .meta files, primitives and ProjectSettings (deterministic, no third-party art).
   python3 samples/pool_spawner/tools/make_sample.py
The script in Assets/Scripts is hand-written; its .meta is only created when missing."""
import json, os, shutil, uuid

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
REPO = os.path.normpath(os.path.join(ROOT, "..", ".."))
CUBE = "00000000-0000-4000-8000-00000000c001"
SCENES = {"Plain": "00000000-0000-4000-a000-000000000b01", "Pooled": "00000000-0000-4000-a000-000000000b02"}
n = [0]
def g():
    n[0] += 1
    return "00000000-0000-4000-9000-%012d" % (0xB000 + n[0])
def wj(rel, obj):
    path = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1)
        f.write("\n")
def obj(name, pos=(0, 0, 0), scale=(1, 1, 1), comps=(), active=True):
    return {"guid": g(), "name": name, "parent": None, "active": active,
            "transform": {"position": list(pos), "rotation": [0, 0, 0, 1], "scale": list(scale)},
            "components": [{"type": t, "fields": f} for t, f in comps], "prefab": None}
def scene(name, pooled):
    objs = [obj("Camera", pos=(0, 0, -12), comps=[("Camera", {"fov": 60.0, "nearClip": 0.3, "farClip": 60.0})]),
            obj("Sun", pos=(0, 10, 0), comps=[("DirectionalLight", {"direction": [-0.4, -1, -0.3], "color": [255, 255, 255, 255]})]),
            obj("ShotTemplate", scale=(0.4, 0.4, 0.4), active=False, comps=[("MeshRenderer", {"mesh": CUBE, "color": [250, 200, 40, 255]})]),
            obj("Spawner", comps=[("ProjectileSpawner", {"pooled": pooled, "perFrame": 2, "lifeFrames": 30, "runFrames": 310})])]
    wj("Assets/Scenes/%s.scene" % name, {"format": 1, "guid": SCENES[name], "objects": objs})
    wj("Assets/Scenes/%s.scene.meta" % name, {"guid": SCENES[name], "importSettings": {}})
scene("Plain", False)
scene("Pooled", True)
wj("ProjectSettings/Project.json", {"format": 1, "name": "PoolSpawner", "target": "psp", "scenes": [SCENES["Plain"], SCENES["Pooled"]],
                                    "componentReserve": {"MeshRenderer": 100}})  # headroom for 96 pooled / 60 cloned shots (PCAP)
ct = json.load(open(os.path.join(REPO, "templates", "psp-3d", "ProjectSettings", "ConsoleTarget.json")))
ct["allowedComponents"] = ["MeshRenderer", "Camera", "DirectionalLight", "ProjectileSpawner"]
wj("ProjectSettings/ConsoleTarget.json", ct)
wj("ProjectSettings/InputActions.json", {"format": 1, "stickDeadZone": 0.2, "stickOuterClamp": 0.95, "actions": []})
src = os.path.join(REPO, "samples", "psp_services", "Assets", "Primitives")
dst = os.path.join(ROOT, "Assets", "Primitives")
os.makedirs(dst, exist_ok=True)
for f in ("cube.prim.json", "cube.prim.json.meta"):
    shutil.copy(os.path.join(src, f), os.path.join(dst, f))
mp = os.path.join(ROOT, "Assets/Scripts/ProjectileSpawner.cpp.meta")
if not os.path.exists(mp):
    wj("Assets/Scripts/ProjectileSpawner.cpp.meta", {"guid": str(uuid.uuid5(uuid.NAMESPACE_DNS, "rensga.pool_spawner.ProjectileSpawner")), "importSettings": {}, "importer": "script"})
print("pool_spawner: scenes and settings written")
