#!/usr/bin/env python3
"""Regenerates the data files of the platformer2d sample: PNG art, .meta files, tilemap, clips, scenes.
Run from anywhere: python3 samples/platformer2d/tools/make_sample.py. The output is committed; scripts are C++ in Assets/Scripts."""
import json, os, struct, zlib

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)


def guid(n):
    return "00000000-0000-4000-8000-%012x" % n


def write_json(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(obj, f, indent=1)
        f.write("\n")


def meta(path, g, settings=None):
    write_json(path + ".meta", {"guid": g, "importSettings": settings or {}})


def png(path, w, h, px):
    raw = b"".join(b"\x00" + bytes(c for p in px[y * w:(y + 1) * w] for c in p) for y in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


T = (0, 0, 0, 0)
# ---- ids -----------------------------------------------------------------------------------------------------
TEX_TILES, TEX_PLAYER, TEX_COIN, TEX_ENEMY, TEX_PROPS = guid(0x101), guid(0x102), guid(0x103), guid(0x104), guid(0x105)
MAP_ROOM = guid(0x111)
ANIM_IDLE, ANIM_RUN, ANIM_JUMP, ANIM_COIN = guid(0x121), guid(0x122), guid(0x123), guid(0x124)
ANIM_WALK, ANIM_HOP_IDLE, ANIM_HOP_AIR, ANIM_FLAG_OFF, ANIM_FLAG_ON = guid(0x125), guid(0x126), guid(0x127), guid(0x128), guid(0x129)
SCENE_TITLE, SCENE_ROOM, SCENE_BOT, SCENE_INPUT = guid(0x301), guid(0x302), guid(0x303), guid(0x304)

# ---- tileset: 8 tiles of 32x32 (0 brick, 1 grass, 2 backdrop, 3 wood platform, 4 slope up-right, 5 slope up-left, 6 one-way plank) ----
BRICK, BRICK_D, GRASS, DIRT, SKY, SKY_D, WOOD, WOOD_D = (150, 70, 50, 255), (110, 45, 35, 255), (70, 170, 70, 255), (120, 85, 50, 255), (60, 90, 150, 255), (70, 100, 160, 255), (200, 150, 80, 255), (150, 105, 55, 255)


def tile_px(t, x, y):
    if t == 0:
        return BRICK_D if (y % 16 == 0 or (x + (16 if (y // 16) % 2 else 0)) % 32 == 0) else BRICK
    if t == 1:
        return GRASS if y < 8 else (DIRT if (x * 7 + y * 3) % 11 else BRICK_D)
    if t == 2:
        return SKY_D if (x % 16 == 8 and y % 16 == 8) else SKY
    if t == 3:
        return WOOD_D if (y < 3 or y > 28) else WOOD
    if t in (4, 5):                                  # 45 degree slopes: surface height = x + 1 (up-right) or 32 - x (up-left)
        xx = x if t == 4 else 31 - x
        top = 31 - xx                                # first solid row of this column
        if y < top:
            return tile_px(2, x, y)
        return GRASS if y < top + 4 else (DIRT if (x * 7 + y * 3) % 11 else BRICK_D)
    if y < 8:                                        # 6: one-way plank, sky below it
        return WOOD_D if (y < 2 or y > 5) else WOOD
    return tile_px(2, x, y)


px = []
for y in range(32):
    for t in range(7):
        for x in range(32):
            px.append(tile_px(t, x, y))
    for x in range(32):
        px.append(tile_px(2, x, y))                  # tile 7: spare backdrop
png(os.path.join(root, "Assets/Textures/tiles.png"), 256, 32, px)
meta(os.path.join(root, "Assets/Textures/tiles.png"), TEX_TILES)

# ---- player sheet: 4 x 2 cells of 32x32 (row 0: idle, idle, jump; row 1: run x4) --------------------------------
SKIN, SHIRT, PANTS, EYE, SHOE = (240, 200, 160, 255), (220, 60, 60, 255), (50, 70, 180, 255), (20, 20, 20, 255), (60, 40, 30, 255)


def player_cell(cell):
    """Returns a 32x32 list of pixels. The figure faces right; the box is x 6..26, y 4..32."""
    img = [[T] * 32 for _ in range(32)]

    def rect(x0, y0, x1, y1, c):
        for yy in range(y0, y1):
            for xx in range(x0, x1):
                img[yy][xx] = c

    bob = 1 if cell == 1 else 0
    rect(10, 4 + bob, 22, 14 + bob, SKIN)           # head
    rect(18, 8 + bob, 20, 10 + bob, EYE)            # eye
    rect(10, 14 + bob, 22, 24, SHIRT)               # body
    rect(10, 24, 22, 28, PANTS)                     # hips
    legs = {0: (0, 0), 1: (0, 0), 2: (-3, 3), 3: (0, 0), 4: (-4, 4), 5: (0, 0), 6: (4, -4), 7: (0, 0)}[cell]
    a, b = legs
    rect(10 + a, 28, 14 + a, 32, SHOE)
    rect(18 + b, 28, 22 + b, 32 if cell != 2 else 30, SHOE)
    if cell == 2:                                   # jump: arms up
        rect(8, 10, 10, 18, SKIN)
        rect(22, 10, 24, 18, SKIN)
    return img


px = []
for cy in range(2):
    for y in range(32):
        for cx in range(4):
            cell = player_cell(cy * 4 + cx)
            px.extend(cell[y])
png(os.path.join(root, "Assets/Textures/player.png"), 128, 64, px)
meta(os.path.join(root, "Assets/Textures/player.png"), TEX_PLAYER)

# ---- coin sheet: 4 frames of 16x16 --------------------------------------------------------------------------------
GOLD, GOLD_D = (250, 210, 40, 255), (200, 140, 20, 255)
px = []
for y in range(16):
    for f in range(4):
        half = (8, 6, 3, 6)[f]
        for x in range(16):
            dx, dy = x - 7.5, y - 7.5
            inside = (dx * dx) / (half * half) + (dy * dy) / 64.0 <= 1.0
            px.append(T if not inside else (GOLD_D if (dx * dx) / (half * half) + (dy * dy) / 64.0 > 0.7 else GOLD))
png(os.path.join(root, "Assets/Textures/coin.png"), 64, 16, px)
meta(os.path.join(root, "Assets/Textures/coin.png"), TEX_COIN)

# ---- enemy sheet: 4 cells of 32x32 (walker step a, walker step b, hopper crouched, hopper in the air) -------------------
PURPLE, PURPLE_D, WHITE, BLACK, GREEN, GREEN_D = (150, 70, 180, 255), (100, 40, 130, 255), (250, 250, 250, 255), (20, 20, 20, 255), (70, 180, 90, 255), (40, 120, 60, 255)


def blank():
    return [[T] * 32 for _ in range(32)]


def rect(img, x0, y0, x1, y1, c):
    for yy in range(y0, y1):
        for xx in range(x0, x1):
            img[yy][xx] = c


def walker_cell(step):
    img = blank()
    rect(img, 6, 14, 26, 28, PURPLE)                 # body
    rect(img, 8, 12, 24, 14, PURPLE)                 # rounded top
    rect(img, 6, 24, 26, 28, PURPLE_D)               # belly
    rect(img, 18, 16, 23, 22, WHITE)                 # eye (the sprite faces right)
    rect(img, 21, 18, 23, 21, BLACK)
    a, b = (0, 4) if step == 0 else (4, 0)           # two feet swap places
    rect(img, 6 + a, 28, 12 + a, 32, BLACK)
    rect(img, 18 + b, 28, 24 + b, 32, BLACK)
    return img


def hopper_cell(air):
    img = blank()
    if air:
        rect(img, 9, 8, 23, 24, GREEN)               # stretched body
        rect(img, 9, 20, 23, 24, GREEN_D)
        rect(img, 6, 24, 12, 30, GREEN_D)            # legs trail
        rect(img, 20, 24, 26, 30, GREEN_D)
        rect(img, 17, 11, 22, 16, WHITE)
        rect(img, 20, 12, 22, 15, BLACK)
    else:
        rect(img, 5, 16, 27, 30, GREEN)              # squat body
        rect(img, 5, 26, 27, 30, GREEN_D)
        rect(img, 4, 30, 12, 32, GREEN_D)
        rect(img, 20, 30, 28, 32, GREEN_D)
        rect(img, 17, 13, 23, 19, WHITE)
        rect(img, 21, 14, 23, 18, BLACK)
    return img


cells = [walker_cell(0), walker_cell(1), hopper_cell(False), hopper_cell(True)]
px = []
for y in range(32):
    for cell in cells:
        px.extend(cell[y])
png(os.path.join(root, "Assets/Textures/enemies.png"), 128, 32, px)
meta(os.path.join(root, "Assets/Textures/enemies.png"), TEX_ENEMY)

# ---- props sheet: 2 cells of 32x32 (save point flag off / on) ---------------------------------------------------------------
GREY, GREY_D = (150, 150, 160, 255), (100, 100, 110, 255)


def flag_cell(on):
    img = blank()
    rect(img, 8, 2, 11, 32, GREY_D)                  # pole
    rect(img, 11, 4, 26, 14, GOLD2 if on else GREY)  # flag
    rect(img, 11, 12, 26, 14, GOLD2_D if on else GREY_D)
    rect(img, 5, 29, 15, 32, GREY_D)                 # base
    return img


GOLD2, GOLD2_D = (250, 210, 40, 255), (200, 140, 20, 255)
cells = [flag_cell(False), flag_cell(True)]
px = []
for y in range(32):
    for cell in cells:
        px.extend(cell[y])
png(os.path.join(root, "Assets/Textures/props.png"), 64, 32, px)
meta(os.path.join(root, "Assets/Textures/props.png"), TEX_PROPS)

# ---- clips ----------------------------------------------------------------------------------------------------------
for name, g, tex, start, count, fps, cw in [("player_idle", ANIM_IDLE, TEX_PLAYER, 0, 2, 2, 32), ("player_run", ANIM_RUN, TEX_PLAYER, 4, 4, 10, 32),
                                            ("player_jump", ANIM_JUMP, TEX_PLAYER, 2, 1, 1, 32), ("coin_spin", ANIM_COIN, TEX_COIN, 0, 4, 8, 16),
                                            ("enemy_walk", ANIM_WALK, TEX_ENEMY, 0, 2, 4, 32), ("hopper_idle", ANIM_HOP_IDLE, TEX_ENEMY, 2, 1, 1, 32),
                                            ("hopper_air", ANIM_HOP_AIR, TEX_ENEMY, 3, 1, 1, 32), ("flag_off", ANIM_FLAG_OFF, TEX_PROPS, 0, 1, 1, 32),
                                            ("flag_on", ANIM_FLAG_ON, TEX_PROPS, 1, 1, 1, 32)]:
    p = os.path.join(root, "Assets/Tiles/%s.anim.json" % name)
    write_json(p, {"format": 1, "name": name, "texture": tex, "fps": fps, "loop": True,
                   "grid": {"cellW": cw, "cellH": cw, "start": start, "count": count}})
    meta(p, g)

# ---- room tilemap 40 x 14 of 32 px (1280 x 448: two screens wide, the Camera2D scrolls) ------------------------------------
W, H = 40, 14
BRICK_T, GRASS_T, SKY_T, WOOD_T, SLOPE_R, SLOPE_L, PLANK = 0, 1, 2, 3, 4, 5, 6
tiles = [SKY_T] * (W * H)
for x in range(W):
    tiles[0 * W + x] = BRICK_T                      # ceiling
    tiles[13 * W + x] = GRASS_T                     # floor
for y in range(H):
    tiles[y * W + 0] = BRICK_T
    tiles[y * W + W - 1] = BRICK_T


def put(c, r, t):
    tiles[r * W + c] = t


# Hill (cols 8..13): a two-step slope run up to a plateau of height 2 (surface y = 352), one slope down, then a ledge.
put(8, 12, SLOPE_R); put(9, 12, BRICK_T); put(9, 11, SLOPE_R)
for c in (10, 11, 12):
    put(c, 11, WOOD_T); put(c, 12, BRICK_T)
put(13, 12, BRICK_T); put(13, 11, SLOPE_L)
# One-way planks: A (cols 17..20, surface y = 320) and B (cols 22..25, surface y = 256).
for c in range(17, 21):
    put(c, 10, PLANK)
for c in range(22, 26):
    put(c, 8, PLANK)
# Stairs at the right end (heights 1, 2, 3, 3, 3 tiles).
for c, h in {34: 1, 35: 2, 36: 3, 37: 3, 38: 3}.items():
    for r in range(13 - h, 13):
        put(c, r, WOOD_T if r == 13 - h else BRICK_T)
p = os.path.join(root, "Assets/Tiles/room.tilemap.json")
write_json(p, {"format": 1, "tileset": TEX_TILES, "tileSize": 32, "width": W, "height": H, "tiles": tiles,
               "solid": [0, 1, 3], "oneWay": [PLANK], "slopes": {str(SLOPE_R): "45R", str(SLOPE_L): "45L"}})
meta(p, MAP_ROOM)

# ---- scenes ---------------------------------------------------------------------------------------------------------
_oid = [0x200]


def obj(name, pos=(0, 0, 0), comps=None, tag=None, parent=None):
    _oid[0] += 1
    o = {"guid": guid(_oid[0]), "name": name, "parent": parent, "active": True,
         "transform": {"position": list(pos), "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
         "components": comps or [], "prefab": None}
    if tag:
        o["tag"] = tag
    return o


def comp(t, **fields):
    return {"type": t, "fields": fields}


white = [255, 255, 255, 255]

# Title
title = [
    obj("Backdrop", comps=[comp("UIImage", x=0, y=0, w=640, h=448, color=[40, 60, 110, 255], sortingOrder=-10)]),
    obj("TitleText", comps=[comp("UIText", x=232, y=140, color=[250, 210, 40, 255])]),
    obj("StartButton", comps=[comp("UIButton", x=232, y=220, w=176, h=32), comp("TitleMenu")]),
    obj("StartLabel", comps=[comp("UIText", x=248, y=232, color=white, sortingOrder=5)]),
    obj("TitleHint", comps=[comp("UIText", x=216, y=300, color=[200, 200, 200, 255])]),
    obj("TitleSaved", comps=[comp("UIText", x=216, y=316, color=[200, 200, 200, 255])]),
    obj("TitleInit", comps=[comp("TitleScreen")]),
]
write_json(os.path.join(root, "Assets/Scenes/Title.scene"), {"format": 1, "guid": SCENE_TITLE, "objects": title})
meta(os.path.join(root, "Assets/Scenes/Title.scene"), SCENE_TITLE)

# Room (and RoomBot: the same objects plus an AutoPilot that plays it)
LAYER_PLAYER, LAYER_ENEMY, LAYER_PICKUP = 1, 2, 3
coins_at = [(168, 392), (360, 328), (600, 296), (760, 232), (846, 392), (1000, 392), (1192, 296)]  # collected in this order


def build_room(with_bot):
    global _oid
    _oid = [0x500 if with_bot else 0x400]  # object GUIDs differ per scene
    objs = [obj("Tilemap", comps=[comp("Tilemap", map=MAP_ROOM), comp("TilemapCollider2D", solidOutside=True)])]
    player = obj("Player", pos=(64, 384, 0), tag="Player", comps=[
        comp("SpriteRenderer", texture=TEX_PLAYER, srcX=0, srcY=0, srcW=32, srcH=32, sortingOrder=5),
        comp("SpriteAnimator", clip=ANIM_IDLE),
        comp("BoxCollider2D", offsetX=6, offsetY=4, sizeX=20, sizeY=28),
        comp("PlayerController2D")])
    player["layer"] = LAYER_PLAYER
    objs.append(player)
    objs.append(obj("Camera", comps=[comp("Camera2D", target=player["guid"], targetOffsetX=16, targetOffsetY=16, deadZoneW=64, deadZoneH=48,
                                          useBounds=True, boundsX=0, boundsY=0, boundsW=W * 32, boundsH=H * 32, pixelSnap=True)]))
    objs += [
        obj("HudCoins", comps=[comp("UIText", x=16, y=40, color=[250, 210, 40, 255])]),
        obj("HudHp", comps=[comp("UIText", x=16, y=52, color=[255, 120, 120, 255])]),
        obj("HudBest", comps=[comp("UIText", x=16, y=64, color=[200, 200, 200, 255])]),
        obj("HudMessage", comps=[comp("UIText", x=208, y=210, color=white, sortingOrder=5)]),
        obj("HudPanel", comps=[comp("UIImage", x=8, y=34, w=152, h=44, color=[0, 0, 0, 140], sortingOrder=-2)]),
        obj("Hud", comps=[comp("HudController")]),
    ]
    for i, (cx, cy) in enumerate(coins_at):
        c = obj("Coin%d" % (i + 1), pos=(cx, cy, 0), tag="Coin", comps=[
            comp("SpriteRenderer", texture=TEX_COIN, srcX=0, srcY=0, srcW=16, srcH=16, sortingOrder=3),
            comp("SpriteAnimator", clip=ANIM_COIN),
            comp("BoxCollider2D", sizeX=16, sizeY=16, isTrigger=True),
            comp("Coin")])
        c["layer"] = LAYER_PICKUP
        objs.append(c)
    sp = obj("SavePoint", pos=(880, 384, 0), tag="SavePoint", comps=[
        comp("SpriteRenderer", texture=TEX_PROPS, srcX=0, srcY=0, srcW=32, srcH=32, sortingOrder=2),
        comp("SpriteAnimator", clip=ANIM_FLAG_OFF),
        comp("BoxCollider2D", offsetX=8, offsetY=0, sizeX=16, sizeY=32, isTrigger=True),
        comp("SavePoint")])
    sp["layer"] = LAYER_PICKUP
    objs.append(sp)
    for name, pos, clip, behaviour in [("Walker1", (560, 384, 0), ANIM_WALK, comp("Walker", patrolMinX=480, patrolMaxX=736)),
                                       ("Hopper1", (1000, 384, 0), ANIM_HOP_IDLE, comp("Hopper", patrolMinX=928, patrolMaxX=1080))]:
        e = obj(name, pos=pos, tag="Enemy", comps=[
            comp("SpriteRenderer", texture=TEX_ENEMY, srcX=0, srcY=0, srcW=32, srcH=32, sortingOrder=4),
            comp("SpriteAnimator", clip=clip),
            comp("BoxCollider2D", offsetX=4, offsetY=12, sizeX=24, sizeY=20),
            behaviour])
        e["layer"] = LAYER_ENEMY
        objs.append(e)
    if with_bot:
        objs.append(obj("AutoPilot", comps=[comp("AutoPilot")]))
    return objs


write_json(os.path.join(root, "Assets/Scenes/Room.scene"), {"format": 1, "guid": SCENE_ROOM, "objects": build_room(False)})
meta(os.path.join(root, "Assets/Scenes/Room.scene"), SCENE_ROOM)
write_json(os.path.join(root, "Assets/Scenes/RoomBot.scene"), {"format": 1, "guid": SCENE_BOT, "objects": build_room(True)})
meta(os.path.join(root, "Assets/Scenes/RoomBot.scene"), SCENE_BOT)

# InputTest (F3.06): every axis, button and key the Input API reports, plus rumble. Not part of the Title -> Room flow.
_oid = [0x600]
gray = [200, 200, 200, 255]
inp = [obj("InBackdrop", comps=[comp("UIImage", x=0, y=0, w=640, h=448, color=[24, 28, 44, 255], sortingOrder=-10)]),
       obj("InTitle", comps=[comp("UIText", x=16, y=12, color=[250, 210, 40, 255])]),
       obj("InHint", comps=[comp("UIText", x=16, y=410, color=gray)]),
       obj("InKeys", comps=[comp("UIText", x=16, y=300, color=white)]),
       obj("InAnyKey", comps=[comp("UIText", x=16, y=316, color=gray)]),
       obj("InRumble", comps=[comp("UIText", x=16, y=340, color=[255, 170, 90, 255])]),
       obj("InHud", comps=[comp("InputTestHud")])]
for i in range(10):
    y = 44 + i * 22
    inp.append(obj("IaL%d" % i, comps=[comp("UIText", x=16, y=y, color=white)]))
    inp.append(obj("IaB%d" % i, comps=[comp("UIImage", x=232, y=y, w=160, h=10, color=[60, 66, 90, 255], sortingOrder=-5)]))
    inp.append(obj("IaF%d" % i, comps=[comp("UIImage", x=232, y=y, w=0, h=10, color=[250, 210, 40, 255], sortingOrder=-4)]))
    if i < 8:
        inp.append(obj("IaC%d" % i, comps=[comp("UIImage", x=311, y=y - 2, w=2, h=14, color=[140, 150, 180, 255], sortingOrder=-3)]))
for i in range(8):
    inp.append(obj("Ib%d" % i, comps=[comp("UIText", x=440, y=44 + i * 22, color=[150, 150, 150, 255])]))
write_json(os.path.join(root, "Assets/Scenes/InputTest.scene"), {"format": 1, "guid": SCENE_INPUT, "objects": inp})
meta(os.path.join(root, "Assets/Scenes/InputTest.scene"), SCENE_INPUT)

# ---- project settings -------------------------------------------------------------------------------------------
write_json(os.path.join(root, "ProjectSettings/Project.json"), {"format": 1, "name": "Platformer2D", "target": "ps2", "scenes": [SCENE_TITLE, SCENE_ROOM, SCENE_BOT, SCENE_INPUT]})
write_json(os.path.join(root, "ProjectSettings/ConsoleTarget.json"), {
    "format": 1, "profile": "ps2", "scalar": "float", "resolution": [640, 448],
    "budgets": {"ramBytes": 33554432, "vramBytes": 4194304, "trianglesPerFrame": 100000, "maxObjects": 256, "maxComponentsPerType": 256},
    "textureFormats": ["clut4", "clut8", "rgba8"], "depth": "zbuffer", "media": "iso", "refreshHz": 60,
    "audio": {"sampleRate": 22050, "voices": 16},
    "capabilities": {"rumble": True},
    "allowedComponents": ["SpriteRenderer", "SpriteAnimator", "Tilemap", "TilemapCollider2D", "BoxCollider2D", "UIText", "UIImage", "UIButton",
                          "Camera2D", "PlayerController2D", "Coin", "Walker", "Hopper", "SavePoint", "AutoPilot", "HudController", "TitleMenu", "TitleScreen", "InputTestHud"]})
write_json(os.path.join(root, "ProjectSettings/InputActions.json"), {"format": 1, "stickDeadZone": 0.2, "stickOuterClamp": 0.95, "actions": [
    {"name": "Move", "kind": "axis2", "source": "nub"},
    {"name": "Pad", "kind": "axis2", "source": "dpad"},
    {"name": "Jump", "kind": "button", "source": "south"},
    # F3.06 (InputTest scene). WASD already feeds the left stick on desktop, so Horizontal / Vertical are the stick;
    # Steer ramps on E / Q (Unity's keyboard axis) and adds the right stick; Keys is a four-key composite.
    {"name": "Horizontal", "kind": "axis", "source": "leftx"},
    {"name": "Vertical", "kind": "axis", "source": "lefty"},
    {"name": "Steer", "kind": "axis", "source": "rightx", "positive": "key:E", "negative": "key:Q", "gravity": 3, "sensitivity": 3, "snap": True},
    {"name": "Aim", "kind": "axis2", "source": "rightstick"},
    {"name": "Keys", "kind": "axis2", "source": "composite", "up": "key:I", "down": "key:K", "left": "key:J", "right": "key:L",
     "gravity": 4, "sensitivity": 6},
    {"name": "TriggerL", "kind": "axis", "source": "l2"},
    {"name": "TriggerR", "kind": "axis", "source": "r2"},
    {"name": "Fire", "kind": "button", "source": "east"},
    {"name": "Use", "kind": "button", "source": "west"},
    {"name": "Special", "kind": "button", "source": "north"},
    {"name": "BumperL", "kind": "button", "source": "l"},
    {"name": "BumperR", "kind": "button", "source": "r"},
    {"name": "Menu", "kind": "button", "source": "start"},
    {"name": "Back", "kind": "button", "source": "select"}]})
write_json(os.path.join(root, "ProjectSettings/Physics2D.json"), {"format": 1, "layers": ["Default", "Player", "Enemy", "Pickup"],
                                                               "ignore": [["Enemy", "Pickup"]]})
print("platformer2d sample data written to", root)
