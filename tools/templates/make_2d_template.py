#!/usr/bin/env python3
"""Generates a 2D Hub template (templates/psp-2d, templates/ps2-2d) from samples/platformer2d.

  python3 tools/templates/make_2d_template.py --profile psp --out templates/psp-2d
  python3 tools/templates/make_2d_template.py --profile ps2 --out templates/ps2-2d
  python3 tools/templates/make_2d_template.py --profile psp --check templates/psp-2d   # exit 1 if the folder differs

It calls samples/platformer2d/tools/make_sample.py (the art, tilemaps, clips, audio and settings are not re-implemented here)
and keeps one room of that game: the tilemap, the player with the shared PlayerController2D, seven coins, the anchored HUD
(HudController) and a Camera2D at the profile resolution. Scripts are copied byte for byte from the sample, so a fix to the
controller reaches every template. Stdlib only and deterministic."""
import argparse, filecmp, json, os, shutil, subprocess, sys, tempfile

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.dirname(os.path.dirname(here))
sample = os.path.join(repo, "samples", "platformer2d")
maker = os.path.join(sample, "tools", "make_sample.py")

SCENE_GUID = "00000000-0000-4000-8000-000000000301"
# Files kept from the sample project (relative paths; every file brings its .meta along).
ASSETS = [
    "Assets/Textures/tiles.png", "Assets/Textures/player.png", "Assets/Textures/coin.png",
    "Assets/Tiles/room.tilemap.json",
    "Assets/Tiles/player_idle.anim.json", "Assets/Tiles/player_run.anim.json", "Assets/Tiles/player_jump.anim.json",
    "Assets/Tiles/coin_spin.anim.json",
    "Assets/Audio/jump.wav", "Assets/Audio/coin.wav", "Assets/Audio/hit.wav",
    "Assets/Scripts/GameState.h", "Assets/Scripts/PlayerController2D.cpp", "Assets/Scripts/Coin.cpp", "Assets/Scripts/HudController.cpp",
    "Streaming/music.wav",
    "ProjectSettings/InputActions.json", "ProjectSettings/Physics2D.json",
]
SCENE_OBJECTS = ("Tilemap", "Player", "Camera", "HudPanelL", "HudPanelC", "HudPanelR", "HudCoins", "HudRoom", "HudLives", "HudHp",
                 "HudTime", "HudMessage", "Hud") + tuple("Coin%d" % i for i in range(1, 8))
COMPONENTS = ["SpriteRenderer", "SpriteAnimator", "Tilemap", "TilemapCollider2D", "BoxCollider2D", "Rigidbody2D", "UIText", "UIImage",
              "UIButton", "Camera2D", "PlayerController2D", "Coin", "HudController"]


def read_json(path):
    with open(path) as f:
        return json.load(f)


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1)
        f.write("\n")


def copy(src_root, rel, out):
    for r in (rel, rel + ".meta"):
        s = os.path.join(src_root, r)
        if os.path.exists(s):
            d = os.path.join(out, r)
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copyfile(s, d)


def generate(profile, out):
    with tempfile.TemporaryDirectory(prefix="rensga-2d-") as tmp:
        src = os.path.join(tmp, "sample")
        subprocess.run([sys.executable, "-I", maker, "--profile", profile, "--out", src], check=True, stdout=subprocess.DEVNULL)
        if os.path.isdir(out):
            shutil.rmtree(out)
        os.makedirs(out)
        for rel in ASSETS:
            copy(src, rel, out)
        # Scene: one room of the game (no enemies, doors, checkpoints or pause menu).
        scene = read_json(os.path.join(src, "Assets/Scenes/Room.scene"))
        keep = [o for o in scene["objects"] if o["name"] in SCENE_OBJECTS]
        assert len(keep) == len(SCENE_OBJECTS), "sample scene changed: %s" % sorted(set(SCENE_OBJECTS) - {o["name"] for o in keep})
        scene["guid"] = SCENE_GUID
        scene["objects"] = keep
        write_json(os.path.join(out, "Assets/Scenes/Main.scene"), scene)
        write_json(os.path.join(out, "Assets/Scenes/Main.scene.meta"), {"guid": SCENE_GUID, "importSettings": {}})
        # Settings: the sample's ConsoleTarget for this profile, restricted to the components the template uses.
        ct = read_json(os.path.join(src, "ProjectSettings/ConsoleTarget.json"))
        ct["allowedComponents"] = [c for c in ct["allowedComponents"] if c in COMPONENTS]
        missing = [c for c in COMPONENTS if c not in ct["allowedComponents"] and c != "Rigidbody2D"]
        assert not missing, "ConsoleTarget lacks %s" % missing
        if "Rigidbody2D" not in ct["allowedComponents"]:
            ct["allowedComponents"].insert(ct["allowedComponents"].index("BoxCollider2D") + 1, "Rigidbody2D")
        write_json(os.path.join(out, "ProjectSettings/ConsoleTarget.json"), ct)
        write_json(os.path.join(out, "ProjectSettings/Project.json"),
                   {"format": 1, "name": "Template2D", "target": profile, "scenes": [SCENE_GUID]})


def same_tree(a, b):
    cmp = filecmp.dircmp(a, b)
    if cmp.left_only or cmp.right_only or cmp.funny_files:
        return False
    _, mismatch, errors = filecmp.cmpfiles(a, b, cmp.common_files, shallow=False)
    if mismatch or errors:
        return False
    return all(same_tree(os.path.join(a, d), os.path.join(b, d)) for d in cmp.common_dirs)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--profile", choices=("psp", "ps2"), required=True)
    ap.add_argument("--out", help="template folder to write (replaced)")
    ap.add_argument("--check", help="compare this folder with a fresh generation (ignores Library/ and thumbnail.png)")
    a = ap.parse_args()
    if a.check:
        with tempfile.TemporaryDirectory(prefix="rensga-2d-check-") as tmp:
            fresh = os.path.join(tmp, "t")
            generate(a.profile, fresh)
            have = os.path.join(tmp, "have")
            shutil.copytree(a.check, have, ignore=shutil.ignore_patterns("Library", "thumbnail.png", "README.md"))
            if not same_tree(fresh, have):
                print("template differs from the generator output: run make_2d_template.py --profile %s --out %s" % (a.profile, a.check))
                return 1
        print("template matches the generator")
        return 0
    if not a.out:
        ap.error("--out or --check is required")
    generate(a.profile, os.path.abspath(a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
