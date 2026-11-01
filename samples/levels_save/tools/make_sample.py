#!/usr/bin/env python3
"""Regenerates samples/levels_save: scenes, .meta files, ProjectSettings and the generated blip sound (no third-party art).
   python3 samples/levels_save/tools/make_sample.py                          # PSP profile, written in place
   python3 samples/levels_save/tools/make_sample.py --profile ps2 --out DIR   # PS2 profile copy (640x448, ISO) for the memory card exercise
The script in Assets/Scripts is hand-written; it is copied with --out and its .meta is only created when missing."""
import argparse, json, math, os, shutil, struct, uuid, wave

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--profile", default="psp", choices=["psp", "ps2"])
ap.add_argument("--out", default=os.path.normpath(os.path.join(HERE, "..")))
args = ap.parse_args()
ROOT = os.path.abspath(args.out)
SRC = os.path.normpath(os.path.join(HERE, ".."))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
SCENES = {"Menu": "00000000-0000-4000-a000-000000000a01", "Loading": "00000000-0000-4000-a000-000000000a02",
          "Level1": "00000000-0000-4000-a000-000000000a03", "Level2": "00000000-0000-4000-a000-000000000a04",
          "Hud": "00000000-0000-4000-a000-000000000a05"}
ADDITIVE = ["Loading", "Hud"]
n = [0]
def g():
    n[0] += 1
    return "00000000-0000-4000-9000-%012d" % (0xA000 + n[0])
def wj(rel, obj):
    path = os.path.join(ROOT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1)
        f.write("\n")
def obj(name, comps=()):
    return {"guid": g(), "name": name, "parent": None, "active": True,
            "transform": {"position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
            "components": [{"type": t, "fields": f} for t, f in comps], "prefab": None}
def image(name, x, y, w, h, color, order, anchor=0):
    return obj(name, [("UIImage", {"anchor": anchor, "x": x, "y": y, "w": w, "h": h, "color": list(color), "sortingOrder": order})])
def text(name, x, y, color=(235, 235, 235, 255), order=5, anchor=0):
    return obj(name, [("UIText", {"anchor": anchor, "x": x, "y": y, "color": list(color), "sortingOrder": order})])
def scene(name, objs):
    wj("Assets/Scenes/%s.scene" % name, {"format": 1, "guid": SCENES[name], "objects": objs})
    wj("Assets/Scenes/%s.scene.meta" % name, {"guid": SCENES[name], "importSettings": {}})

W, H = (480, 272) if args.profile == "psp" else (640, 448)   # the profile's screen: positions below are in its pixels
GOLD = (250, 200, 40, 255)
# Menu: the flow object lives here and survives into the levels (DontDestroyOnLoad)
scene("Menu", [image("Backdrop", 0, 0, W, H, (22, 26, 48, 255), -10),
               image("Band", 0, 70, W, 56, (40, 48, 90, 255), -5),
               text("MenuTitle", 20, 86, GOLD),
               text("MenuLine", 20, 150), text("MenuRuns", 20, 176, (150, 160, 190, 255)),
               text("MenuHint", 20, 210, (150, 160, 190, 255)),
               obj("Flow", [("LevelFlow", {"minLoadFrames": 90 if args.profile == "psp" else 300, "chunk": 2, "afterSave": 1 if args.profile == "ps2" else 0, "menuFrames": 40})])])
# Loading: loaded additively over whatever is there; Flow drives BarFill and BarPct from AsyncOperation.progress
BX, BY = (W - 308) // 2, H // 2 - 11
scene("Loading", [image("LoadShade", 0, 0, W, H, (8, 10, 24, 255), 50),
                  text("LoadTitle", 0, -50, GOLD, 60, 4),
                  image("BarBack", BX, BY, 308, 22, (60, 66, 100, 255), 55),
                  image("BarFill", BX + 4, BY + 4, 4, 14, GOLD, 56),
                  text("BarPct", 0, 40, (235, 235, 235, 255), 60, 4)])
def level(name, bg, tile):
    objs = [image("Backdrop", 0, 0, W, H, bg, -10), text("LevelTitle", 12, 36, GOLD, 5)]
    cw, ch = (W - 24) // 8, (H - 70) // 6
    for j in range(6):  # 48 tiles so the load has something to build
        for i in range(8):
            c = tile if (i + j) % 2 == 0 else tuple(max(0, v - 24) for v in tile[:3]) + (255,)
            objs.append(image("T%d" % (j * 8 + i), 12 + i * cw + 2, 62 + j * ch + 2, cw - 4, ch - 4, c, -5))
    return objs
l1 = level("Level1", (30, 70, 40, 255), (70, 150, 80, 255))
l2 = level("Level2", (70, 36, 30, 255), (170, 100, 70, 255))
scene("Level1", l1)
scene("Level2", l2)
scene("Hud", [image("HudBar", 0, 0, W, 28, (0, 0, 0, 170), 80), text("HudLevel", 8, 6, GOLD, 90), text("HudCoins", 160, 6, (235, 235, 235, 255), 90)])

wj("ProjectSettings/Project.json", {"format": 1, "name": "LevelsSave", "target": args.profile,
                                    "scenes": [SCENES[k] for k in ("Menu", "Loading", "Level1", "Level2", "Hud")],
                                    "additiveScenes": [SCENES[k] for k in ADDITIVE]})
if args.profile == "psp":
    ct = json.load(open(os.path.join(REPO, "templates", "psp-3d", "ProjectSettings", "ConsoleTarget.json")))
else:
    ct = json.load(open(os.path.join(REPO, "templates", "ps2-2d", "ProjectSettings", "ConsoleTarget.json")))
ct["allowedComponents"] = ["UIText", "UIImage", "LevelFlow"]
wj("ProjectSettings/ConsoleTarget.json", ct)
wj("ProjectSettings/InputActions.json", {"format": 1, "stickDeadZone": 0.2, "stickOuterClamp": 0.95, "actions": [
    {"name": "Confirm", "kind": "button", "source": "south"}]})

# Assets/Resources/sfx/blip.wav: 0.12 s sine, 22050 Hz mono 16 bit (generated)
wavp = os.path.join(ROOT, "Assets", "Resources", "sfx", "blip.wav")
os.makedirs(os.path.dirname(wavp), exist_ok=True)
with wave.open(wavp, "wb") as w:
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(22050)
    frames = bytearray()
    for i in range(int(22050 * 0.12)):
        env = 1.0 - i / (22050 * 0.12)
        frames += struct.pack("<h", int(9000 * env * math.sin(2 * math.pi * 880 * i / 22050)))
    w.writeframes(bytes(frames))
wj("Assets/Resources/sfx/blip.wav.meta", {"guid": "00000000-0000-4000-8000-00000000a0b1", "importSettings": {}})

# the script (copied for --out) and its meta
os.makedirs(os.path.join(ROOT, "Assets", "Scripts"), exist_ok=True)
if ROOT != SRC:
    shutil.copy(os.path.join(SRC, "Assets", "Scripts", "LevelFlow.cpp"), os.path.join(ROOT, "Assets", "Scripts", "LevelFlow.cpp"))
mp = os.path.join(ROOT, "Assets/Scripts/LevelFlow.cpp.meta")
if not os.path.exists(mp):
    wj("Assets/Scripts/LevelFlow.cpp.meta", {"guid": str(uuid.uuid5(uuid.NAMESPACE_DNS, "rensga.levels_save.LevelFlow")), "importSettings": {}, "importer": "script"})
print("levels_save (%s): scenes and settings written to %s" % (args.profile, ROOT))
