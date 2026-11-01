"""Shared generator code for samples/animator_player and samples/footsteps (F6.03 / F6.10): ProjectSettings, the player sheet, the
property clips and the Player controller as plain JSON, the scene builder and the generated step sound. Deterministic; no third-party art
(the player sheet is the one the platformer2d sample already generates)."""
import json, math, os, shutil, struct, wave

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
PLAYER_TEX = "00000000-0000-4000-8000-000000000102"
WHITE_TEX = "00000000-0000-4000-8000-00000000d101"
SRC_PSP2D = os.path.join(REPO, "samples", "platformer2d_psp")


class Gen:
    def __init__(self, root, name, tag):
        self.root, self.name, self.tag = root, name, tag
        self.n = 0
        self.scene_guid = "00000000-0000-4000-a000-%012d" % (0xA000 + tag)

    def g(self):
        self.n += 1
        return "00000000-0000-4000-9000-%012d" % (tag_base(self.tag) + self.n)

    def wj(self, rel, obj):
        path = os.path.join(self.root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            json.dump(obj, f, indent=1)
            f.write("\n")

    def meta(self, rel, guid, extra=None):
        d = {"guid": guid, "importSettings": {}}
        d.update(extra or {})
        self.wj(rel + ".meta", d)

    def obj(self, name, pos=(0, 0, 0), scale=(1, 1, 1), comps=(), parent=None, active=True):
        return {"guid": self.g(), "name": name, "parent": parent, "active": active,
                "transform": {"position": list(pos), "rotation": [0, 0, 0, 1], "scale": list(scale)},
                "components": [{"type": t, "fields": f} for t, f in comps], "prefab": None}


def tag_base(tag):
    return 0xA100 + tag * 0x100


def settings(gen, scripts, extra_components=()):
    ct = json.load(open(os.path.join(SRC_PSP2D, "ProjectSettings", "ConsoleTarget.json")))
    ct["allowedComponents"] = ["SpriteRenderer", "UIText", "UIImage", "Animator", "AudioSource"] + list(scripts) + list(extra_components)
    gen.wj("ProjectSettings/ConsoleTarget.json", ct)
    gen.wj("ProjectSettings/InputActions.json", {"format": 1, "stickDeadZone": 0.2, "stickOuterClamp": 0.95, "actions": []})
    gen.wj("ProjectSettings/Project.json", {"format": 1, "name": gen.name, "target": "psp", "scenes": [gen.scene_guid]})


def copy_player(gen):
    dst = os.path.join(gen.root, "Assets", "Textures")
    os.makedirs(dst, exist_ok=True)
    for f in ("player.png", "player.png.meta"):
        shutil.copy(os.path.join(SRC_PSP2D, "Assets", "Textures", f), os.path.join(dst, f))


def script_meta(gen, cls):
    rel = "Assets/Scripts/%s.cpp.meta" % cls
    if not os.path.exists(os.path.join(gen.root, rel)):
        import uuid
        gen.wj(rel, {"guid": str(uuid.uuid5(uuid.NAMESPACE_DNS, "rensga.%s.%s" % (gen.name, cls))), "importSettings": {}, "importer": "script"})


def key(t, v, tin=0.0, tout=0.0):
    k = {"t": t, "v": v}
    if tin or tout:
        k["in"], k["out"] = tin, tout
    return k


def sheet_curves(src_x, src_y):
    """SpriteRenderer srcX / srcY step curves; src_x = [(tick, x)], src_y = [(tick, y)]."""
    return [{"property": "SpriteRenderer.srcX", "interp": "step", "keys": [key(t, v) for t, v in src_x]},
            {"property": "SpriteRenderer.srcY", "interp": "step", "keys": [key(t, v) for t, v in src_y]}]


def clips(with_events=False):
    idle = {"format": 1, "kind": "PropertyClip", "name": "Idle", "fps": 10, "length": 10, "loop": True,
            "curves": sheet_curves([(0, 0), (5, 32)], [(0, 0)]), "events": []}
    run_events = [{"t": 0, "function": "Footstep", "float": 1.0}, {"t": 2, "function": "Footstep", "float": 0.7}] if with_events else []
    run = {"format": 1, "kind": "PropertyClip", "name": "Run", "fps": 12, "length": 4, "loop": True,
           "curves": sheet_curves([(0, 0), (1, 32), (2, 64), (3, 96)], [(0, 32)]), "events": run_events}
    jump = {"format": 1, "kind": "PropertyClip", "name": "Jump", "fps": 10, "length": 8, "loop": False,
            "curves": sheet_curves([(0, 64)], [(0, 0)]) + [
                {"property": "Transform.localScale.y", "interp": "smooth", "keys": [key(0, 4.0), key(3, 4.7), key(8, 4.0)]},
                {"property": "Transform.localScale.x", "interp": "smooth", "keys": [key(0, 4.0), key(3, 3.6), key(8, 4.0)]}], "events": []}
    return {"Idle": idle, "Run": run, "Jump": jump}


def controller(guids, with_jump=True):
    states = [{"name": "Idle", "motion": guids["Idle"], "loop": True, "pos": [60, 80]},
              {"name": "Run", "motion": guids["Run"], "loop": True, "pos": [300, 80]}]
    trans = [{"from": "Idle", "to": "Run", "duration": 0.08, "conditions": [{"param": "speed", "mode": "greater", "value": 0.1}]},
             {"from": "Run", "to": "Idle", "duration": 0.08, "conditions": [{"param": "speed", "mode": "less", "value": 0.1}]}]
    params = [{"name": "speed", "type": "float", "default": 0}]
    if with_jump:
        params.append({"name": "jump", "type": "trigger"})
        states.append({"name": "Jump", "motion": guids["Jump"], "loop": False, "pos": [180, 220]})
        trans.append({"from": "*", "to": "Jump", "duration": 0.05, "conditions": [{"param": "jump", "mode": "if"}]})
        trans.append({"from": "Jump", "to": "Idle", "hasExitTime": True, "exitTime": 1.0, "duration": 0.05})
    return {"format": 1, "kind": "AnimatorController", "name": "Player", "parameters": params, "defaultState": "Idle",
            "states": states, "transitions": trans}


def write_anim_assets(gen, with_jump, with_events):
    cl = clips(with_events)
    if not with_jump:
        cl.pop("Jump")
    guids = {}
    for i, (n, c) in enumerate(sorted(cl.items())):
        guids[n] = "00000000-0000-4000-8000-%012d" % (0xC100 + gen.tag * 16 + i + 1)
        gen.wj("Assets/Animation/%s.clip.json" % n, c)
        gen.meta("Assets/Animation/%s.clip.json" % n, guids[n])
    cg = "00000000-0000-4000-8000-%012d" % (0xC200 + gen.tag)
    gen.wj("Assets/Animation/Player.controller.json", controller(guids, with_jump))
    gen.meta("Assets/Animation/Player.controller.json", cg)
    return guids, cg


def white_texture(gen):
    """A 4x4 white PNG: flat colored rectangles are sprites of it, scaled and tinted (a UIImage always draws above sprites)."""
    import zlib
    raw = b"".join(b"\x00" + b"\xff\xff\xff\xff" * 4 for _ in range(4))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    path = os.path.join(gen.root, "Assets", "Textures", "white.png")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
    gen.meta("Assets/Textures/white.png", WHITE_TEX)


def rect(gen, name, x, y, w, h, color, order):
    return gen.obj(name, pos=(x, y, 0), scale=(w / 4.0, h / 4.0, 1),
                   comps=[("SpriteRenderer", {"texture": WHITE_TEX, "srcX": 0, "srcY": 0, "srcW": 4, "srcH": 4, "color": color, "sortingOrder": order})])


def backdrop(gen):
    white_texture(gen)
    return [rect(gen, "Sky", 0, 0, 480, 272, [86, 128, 190, 255], -5), rect(gen, "Floor", 0, 222, 480, 50, [92, 70, 52, 255], -4),
            rect(gen, "Grass", 0, 222, 480, 4, [84, 160, 80, 255], -3)]


def player_obj(gen, extra, ctl_guid, x=60, y=222):
    comps = [("SpriteRenderer", {"texture": PLAYER_TEX, "srcX": 0, "srcY": 0, "srcW": 32, "srcH": 32, "pivotX": 16, "pivotY": 32, "sortingOrder": 1}),
             ("Animator", {"controller": ctl_guid, "speed": 1.0})] + list(extra)
    return gen.obj("Player", pos=(x, y, 0), scale=(4, 4, 1), comps=comps)


def label(gen, name, x, y):
    return gen.obj(name, comps=[("UIText", {"anchor": 0, "x": x, "y": y, "color": [255, 255, 255, 255], "sortingOrder": 5})])


def write_wav(path, samples, rate=22050):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", max(-32768, min(32767, int(s)))) for s in samples))


def step_sound(rate=22050):
    """A dull footstep: a 95 Hz thump plus a short burst of noise (LCG, so it is the same on every machine), 0.14 s."""
    n, seed, out = int(rate * 0.14), 12345, []
    for i in range(n):
        t = i / rate
        seed = (seed * 1664525 + 1013904223) & 0xFFFFFFFF
        noise = ((seed >> 8) & 0xFFFF) / 32768.0 - 1.0
        env = math.exp(-t * 38.0)
        out.append(20000 * env * (0.65 * math.sin(2 * math.pi * 95 * t) + 0.35 * noise))
    return out


def hum_sound(rate=22050):
    """One second of a soft two-tone hum; whole cycles, so the loop is seamless."""
    return [3500 * math.sin(2 * math.pi * 110 * i / rate) + 1800 * math.sin(2 * math.pi * 165 * i / rate) for i in range(rate)]
