#!/usr/bin/env python3
"""Regenerates samples/frame_spike: the two scenes (Spiky, Smooth), .meta files, primitives and ProjectSettings (deterministic, no art).
   python3 samples/frame_spike/tools/make_sample.py
The script in Assets/Scripts is hand-written; its .meta is only created when missing."""
import json, os, shutil, uuid

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
REPO = os.path.normpath(os.path.join(ROOT, "..", ".."))
CUBE = "00000000-0000-4000-8000-00000000c001"
SCENES = {"Spiky": "00000000-0000-4000-a000-000000000d01", "Smooth": "00000000-0000-4000-a000-000000000d02"}
n = [0]
def g():
    n[0] += 1
    return "00000000-0000-4000-9000-%012d" % (0xD000 + n[0])
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
def scene(name, spread):
    objs = [obj("Camera", pos=(0, 0.5, -6), comps=[("Camera", {"fov": 60.0, "nearClip": 0.3, "farClip": 60.0})]),
            obj("Sun", pos=(0, 10, 0), comps=[("DirectionalLight", {"direction": [-0.4, -1, -0.3], "color": [255, 255, 255, 255]})]),
            obj("Cube", scale=(1.6, 1.6, 1.6), comps=[("MeshRenderer", {"mesh": CUBE, "color": [250, 200, 40, 255]}), ("Spinner", {"degreesPerSecond": 60})]),
            obj("Planner", comps=[("RoutePlanner", {"spread": spread, "period": 120, "work": 1500000, "sliceFrames": 40, "runFrames": 730})])]
    wj("Assets/Scenes/%s.scene" % name, {"format": 1, "guid": SCENES[name], "objects": objs})
    wj("Assets/Scenes/%s.scene.meta" % name, {"guid": SCENES[name], "importSettings": {}})
scene("Spiky", False)
scene("Smooth", True)
wj("ProjectSettings/Project.json", {"format": 1, "name": "FrameSpike", "target": "psp", "scenes": [SCENES["Spiky"], SCENES["Smooth"]]})
ct = json.load(open(os.path.join(REPO, "templates", "psp-3d", "ProjectSettings", "ConsoleTarget.json")))
ct["allowedComponents"] = ["MeshRenderer", "Camera", "DirectionalLight", "RoutePlanner", "Spinner"]
wj("ProjectSettings/ConsoleTarget.json", ct)
wj("ProjectSettings/InputActions.json", {"format": 1, "stickDeadZone": 0.2, "stickOuterClamp": 0.95, "actions": []})
src = os.path.join(REPO, "samples", "psp_services", "Assets", "Primitives")
dst = os.path.join(ROOT, "Assets", "Primitives")
os.makedirs(dst, exist_ok=True)
for f in ("cube.prim.json", "cube.prim.json.meta"):
    shutil.copy(os.path.join(src, f), os.path.join(dst, f))
mp = os.path.join(ROOT, "Assets/Scripts/RoutePlanner.cpp.meta")
if not os.path.exists(mp):
    wj("Assets/Scripts/RoutePlanner.cpp.meta", {"guid": str(uuid.uuid5(uuid.NAMESPACE_DNS, "rensga.frame_spike.RoutePlanner")), "importSettings": {}, "importer": "script"})
print("frame_spike: scenes and settings written")
