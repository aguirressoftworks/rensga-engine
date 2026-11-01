#!/usr/bin/env python3
"""samples/menus (F6.04 / F6.12): project skeleton for the title + pause menu exercise. Deterministic, no art.
   python3 samples/menus/tools/make_sample.py [--profile psp|ps2] [--out <dir>]
Writes ProjectSettings, empty scene files (Title, Game, TitleBot) with their .meta, and the script .meta files. The scenes are then FILLED BY THE
EDITOR: samples/menus/tools/author_in_editor.py drives rensga-editor with --ui-event verbs (GameObject > UI, UnityEvents, scripts), so the
scene files in the sample are what the editor saved. Default profile psp (480x272) into samples/menus; the same scenes run at 640x448 on
the ps2 profile through the Canvas scaler (--profile ps2 --out <dir> writes only the settings, scenes and scripts are shared by copy)."""
import argparse, json, os, shutil, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE = os.path.normpath(os.path.join(HERE, ".."))
REPO = os.path.normpath(os.path.join(SAMPLE, "..", ".."))
SCENES = {"Title": "00000000-0000-4000-a000-0000000d1001", "Game": "00000000-0000-4000-a000-0000000d1002", "TitleBot": "00000000-0000-4000-a000-0000000d1003"}
COMPONENTS = ["UIText", "UIImage", "UIButton", "Canvas", "Slider", "Toggle", "LayoutGroup", "EventSystem", "MenuFlow", "MenuBot"]

ap = argparse.ArgumentParser()
ap.add_argument("--profile", default="psp", choices=["psp", "ps2"])
ap.add_argument("--out", default=SAMPLE)
args = ap.parse_args()
OUT = os.path.abspath(args.out)

def wj(rel, obj):
    path = os.path.join(OUT, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1)
        f.write("\n")

src = "platformer2d_psp" if args.profile == "psp" else "platformer2d"
ct = json.load(open(os.path.join(REPO, "samples", src, "ProjectSettings", "ConsoleTarget.json")))
ct["allowedComponents"] = COMPONENTS
wj("ProjectSettings/ConsoleTarget.json", ct)
wj("ProjectSettings/Project.json", {"format": 1, "name": "Menus", "target": args.profile, "scenes": [SCENES["Title"], SCENES["Game"], SCENES["TitleBot"]]})
wj("ProjectSettings/InputActions.json", {"format": 1, "stickDeadZone": 0.2, "stickOuterClamp": 0.95,
                                          "actions": [{"name": "Menu", "kind": "button", "source": "start", "alt": "key:Escape"}]})
for name, guid in SCENES.items():
    sp = os.path.join(OUT, "Assets", "Scenes", name + ".scene")
    if not os.path.exists(sp):  # never overwrite what the editor saved
        authored = os.path.join(SAMPLE, "Assets", "Scenes", name + ".scene")
        if OUT != SAMPLE and os.path.exists(authored):  # another profile: the same scenes, saved by the editor
            os.makedirs(os.path.join(OUT, "Assets", "Scenes"), exist_ok=True)
            shutil.copy(authored, sp)
        else:
            wj("Assets/Scenes/%s.scene" % name, {"format": 1, "guid": guid, "objects": []})
    wj("Assets/Scenes/%s.scene.meta" % name, {"guid": guid, "importSettings": {}})
os.makedirs(os.path.join(OUT, "Assets", "Scripts"), exist_ok=True)
if OUT != SAMPLE:
    for f in os.listdir(os.path.join(SAMPLE, "Assets", "Scripts")):
        shutil.copy(os.path.join(SAMPLE, "Assets", "Scripts", f), os.path.join(OUT, "Assets", "Scripts", f))
for name in ("MenuFlow.cpp", "MenuBot.cpp", "MenuShared.h"):
    mp = os.path.join(OUT, "Assets", "Scripts", name + ".meta")
    if not os.path.exists(mp):
        wj("Assets/Scripts/%s.meta" % name, {"guid": str(uuid.uuid5(uuid.NAMESPACE_DNS, "rensga.menus." + name)), "importSettings": {}, "importer": "script"})
print("menus: project skeleton written to %s (profile %s)" % (OUT, args.profile))
