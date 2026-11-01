#!/usr/bin/env python3
"""samples/hud_options (F6.04 / F6.12): project skeleton for the HUD + options exercise. Deterministic, no art.
   python3 samples/hud_options/tools/make_sample.py [--profile psp|ps2] [--out <dir>]
Writes ProjectSettings, empty scenes (Hud, Options, OptionsVerify, HudBot) with .meta, and the script .meta files; the scenes are then filled by
the editor with samples/hud_options/tools/author_in_editor.py (rensga-editor --ui-event verbs). Existing scene files are never overwritten."""
import argparse, json, os, shutil, uuid

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE = os.path.normpath(os.path.join(HERE, ".."))
REPO = os.path.normpath(os.path.join(SAMPLE, "..", ".."))
SCENES = {"Hud": "00000000-0000-4000-a000-0000000d2001", "Options": "00000000-0000-4000-a000-0000000d2002",
          "OptionsVerify": "00000000-0000-4000-a000-0000000d2003", "HudBot": "00000000-0000-4000-a000-0000000d2004"}
COMPONENTS = ["UIText", "UIImage", "UIButton", "Canvas", "Slider", "Toggle", "LayoutGroup", "EventSystem", "HudDemo", "OptionsMenu", "HudBot"]
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
wj("ProjectSettings/Project.json", {"format": 1, "name": "HudOptions", "target": args.profile, "scenes": [SCENES[n] for n in ("Hud", "Options", "OptionsVerify", "HudBot")]})
wj("ProjectSettings/InputActions.json", {"format": 1, "stickDeadZone": 0.2, "stickOuterClamp": 0.95, "actions": []})
for name, guid in SCENES.items():
    if not os.path.exists(os.path.join(OUT, "Assets", "Scenes", name + ".scene")):
        authored = os.path.join(SAMPLE, "Assets", "Scenes", name + ".scene")
        if OUT != SAMPLE and os.path.exists(authored):  # another profile: the same scenes, saved by the editor
            os.makedirs(os.path.join(OUT, "Assets", "Scenes"), exist_ok=True)
            shutil.copy(authored, os.path.join(OUT, "Assets", "Scenes", name + ".scene"))
        else:
            wj("Assets/Scenes/%s.scene" % name, {"format": 1, "guid": guid, "objects": []})
    wj("Assets/Scenes/%s.scene.meta" % name, {"guid": guid, "importSettings": {}})
os.makedirs(os.path.join(OUT, "Assets", "Scripts"), exist_ok=True)
if OUT != SAMPLE:
    for f in os.listdir(os.path.join(SAMPLE, "Assets", "Scripts")):
        shutil.copy(os.path.join(SAMPLE, "Assets", "Scripts", f), os.path.join(OUT, "Assets", "Scripts", f))
for name in ("HudDemo.cpp", "OptionsMenu.cpp", "HudBot.cpp"):
    mp = os.path.join(OUT, "Assets", "Scripts", name + ".meta")
    if not os.path.exists(mp):
        wj("Assets/Scripts/%s.meta" % name, {"guid": str(uuid.uuid5(uuid.NAMESPACE_DNS, "rensga.hud_options." + name)), "importSettings": {}, "importer": "script"})
print("hud_options: project skeleton written to %s (profile %s)" % (OUT, args.profile))
