#!/usr/bin/env python3
"""Regenerates samples/psp_services: scenes, .meta files and ProjectSettings (deterministic, no third-party art).
   python3 samples/psp_services/tools/make_sample.py          # writes in place
The scripts in Assets/Scripts are hand-written and are not touched (their .meta files are only created when missing)."""
import json, os, uuid

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
CUBE, PLANE = "00000000-0000-4000-8000-00000000c001", "00000000-0000-4000-8000-00000000c004"
SCENES = {"Services": "00000000-0000-4000-a000-000000000601", "ServicesAuto": "00000000-0000-4000-a000-000000000602",
          "InputCam": "00000000-0000-4000-a000-000000000603"}
n = [0]
def g():
    n[0] += 1
    return "00000000-0000-4000-9000-%012d" % (0x7000 + n[0])
def wj(rel, obj):
    path = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1)
        f.write("\n")
def obj(name, pos=(0, 0, 0), scale=(1, 1, 1), rot=(0, 0, 0, 1), comps=()):
    return {"guid": g(), "name": name, "parent": None, "active": True,
            "transform": {"position": list(pos), "rotation": list(rot), "scale": list(scale)},
            "components": [{"type": t, "fields": f} for t, f in comps], "prefab": None}
def mesh(prim, color):
    return ("MeshRenderer", {"mesh": prim, "color": list(color)})
def text(name, x, y, color=(235, 235, 235, 255)):
    return obj(name, comps=[("UIText", {"anchor": 0, "x": x, "y": y, "color": list(color), "sortingOrder": 5})])
def scene(name, objs):
    wj("Assets/Scenes/%s.scene" % name, {"format": 1, "guid": SCENES[name], "objects": objs})
    wj("Assets/Scenes/%s.scene.meta" % name, {"guid": SCENES[name], "importSettings": {}})

def ui_lines(first_y=22):
    o = [obj("Backdrop", comps=[("UIImage", {"anchor": 0, "x": 0, "y": 0, "w": 480, "h": 272, "color": [20, 24, 40, 255], "sortingOrder": -10})])]
    o.append(obj("Camera", pos=(0, 0, -5), comps=[("Camera", {"fov": 60.0, "nearClip": 0.3, "farClip": 50.0})]))
    for i in range(8):
        o.append(text("S%d" % i, 12, 10 + i * 22, (250, 210, 40, 255) if i == 0 else (235, 235, 235, 255)))
    return o

def services(use_osk):
    o = ui_lines()
    o.append(obj("Services", comps=[("ServicesDemo", {"useOsk": use_osk})]))
    return o
scene("Services", services(True))
scene("ServicesAuto", services(False))

cam = [obj("Camera", pos=(0, 3, -7), comps=[("Camera", {"fov": 60.0, "nearClip": 0.3, "farClip": 80.0}),
                                               ("CamRig", {"distance": 7.0, "orbitSpeed": 100.0, "moveSpeed": 4.0})])]
cam.append(obj("Sun", pos=(0, 10, 0), comps=[("DirectionalLight", {"direction": [-0.4, -1, -0.3], "color": [255, 255, 255, 255]})]))
cam.append(obj("Ground", pos=(0, -0.5, 0), scale=(40, 1, 40), comps=[mesh(PLANE, (70, 120, 70, 255))]))
cam.append(obj("Player", pos=(0, 0.5, 0), comps=[mesh(CUBE, (240, 200, 60, 255))]))
marks = [("North", (0, 1, 12), (230, 70, 70, 255)), ("East", (12, 1, 0), (70, 140, 230, 255)),
         ("South", (0, 1, -12), (230, 230, 230, 255)), ("West", (-12, 1, 0), (150, 80, 220, 255))]
for nm, p, c in marks:
    cam.append(obj(nm, pos=p, scale=(2, 2, 2), comps=[mesh(CUBE, c)]))
cam.append(text("CamLine", 8, 8, (255, 255, 255, 255)))
scene("InputCam", cam)

wj("ProjectSettings/Project.json", {"format": 1, "name": "PspServices", "target": "psp",
                                    "scenes": [SCENES["Services"], SCENES["ServicesAuto"], SCENES["InputCam"]]})
wj("ProjectSettings/InputActions.json", {"format": 1, "stickDeadZone": 0.2, "stickOuterClamp": 0.95, "actions": [
    {"name": "Move", "kind": "axis2", "source": "nub"},
    {"name": "CameraLeft", "kind": "button", "source": "l", "alt": "west"},
    {"name": "CameraRight", "kind": "button", "source": "r", "alt": "east"},
    {"name": "CameraUp", "kind": "button", "source": "up"},
    {"name": "CameraDown", "kind": "button", "source": "down"},
    {"name": "ConfirmSouth", "kind": "button", "source": "south"},
    {"name": "ConfirmEast", "kind": "button", "source": "east"},
    {"name": "Menu", "kind": "button", "source": "start"}]})
ct = json.load(open(os.path.join(ROOT, "..", "..", "templates", "psp-3d", "ProjectSettings", "ConsoleTarget.json")))
ct["allowedComponents"] = ["MeshRenderer", "Camera", "DirectionalLight", "UIText", "UIImage", "CamRig", "ServicesDemo"]
wj("ProjectSettings/ConsoleTarget.json", ct)
for s in ("CamRig", "ServicesDemo"):
    mp = os.path.join(ROOT, "Assets/Scripts/%s.cpp.meta" % s)
    if not os.path.exists(mp):
        wj("Assets/Scripts/%s.cpp.meta" % s, {"guid": str(uuid.uuid5(uuid.NAMESPACE_DNS, "rensga.psp_services." + s)), "importSettings": {}, "importer": "script"})
print("psp_services: scenes and settings written")
