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
TEX_TILES, TEX_PLAYER, TEX_COIN = guid(0x101), guid(0x102), guid(0x103)
MAP_ROOM = guid(0x111)
ANIM_IDLE, ANIM_RUN, ANIM_JUMP, ANIM_COIN = guid(0x121), guid(0x122), guid(0x123), guid(0x124)
SCENE_TITLE, SCENE_ROOM = guid(0x301), guid(0x302)

# ---- tileset: 4 tiles of 32x32 (0 brick, 1 grass, 2 backdrop, 3 platform) ---------------------------------------
BRICK, BRICK_D, GRASS, DIRT, SKY, SKY_D, WOOD, WOOD_D = (150, 70, 50, 255), (110, 45, 35, 255), (70, 170, 70, 255), (120, 85, 50, 255), (60, 90, 150, 255), (70, 100, 160, 255), (200, 150, 80, 255), (150, 105, 55, 255)


def tile_px(t, x, y):
    if t == 0:
        return BRICK_D if (y % 16 == 0 or (x + (16 if (y // 16) % 2 else 0)) % 32 == 0) else BRICK
    if t == 1:
        return GRASS if y < 8 else (DIRT if (x * 7 + y * 3) % 11 else BRICK_D)
    if t == 2:
        return SKY_D if (x % 16 == 8 and y % 16 == 8) else SKY
    return WOOD_D if (y < 3 or y > 28) else WOOD


px = []
for y in range(32):
    for t in range(4):
        for x in range(32):
            px.append(tile_px(t, x, y))
png(os.path.join(root, "Assets/Textures/tiles.png"), 128, 32, px)
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

# ---- clips ----------------------------------------------------------------------------------------------------------
for name, g, tex, start, count, fps, cw in [("player_idle", ANIM_IDLE, TEX_PLAYER, 0, 2, 2, 32), ("player_run", ANIM_RUN, TEX_PLAYER, 4, 4, 10, 32),
                                            ("player_jump", ANIM_JUMP, TEX_PLAYER, 2, 1, 1, 32), ("coin_spin", ANIM_COIN, TEX_COIN, 0, 4, 8, 16)]:
    p = os.path.join(root, "Assets/Tiles/%s.anim.json" % name)
    write_json(p, {"format": 1, "name": name, "texture": tex, "fps": fps, "loop": True,
                   "grid": {"cellW": cw, "cellH": cw, "start": start, "count": count}})
    meta(p, g)

# ---- room tilemap 20 x 14 of 32 px (640 x 448) -----------------------------------------------------------------------
W, H = 20, 14
tiles = [65535] * (W * H)
for x in range(W):
    for y in range(H):
        tiles[y * W + x] = 2                        # backdrop everywhere
for x in range(W):
    tiles[0 * W + x] = 0                            # ceiling
    tiles[13 * W + x] = 1                           # floor (grass)
for y in range(H):
    tiles[y * W + 0] = 0
    tiles[y * W + 19] = 0
# Two staircases of 32 px steps (heights in tiles per column): the player climbs them by jumping.
heights = {2: 1, 3: 2, 4: 3, 11: 1, 12: 2, 13: 3, 14: 4, 15: 5, 16: 5, 17: 5, 18: 5}
for c, h in heights.items():
    for r in range(13 - h, 13):
        tiles[r * W + c] = 3 if r == 13 - h else 0   # wood cap, brick body
p = os.path.join(root, "Assets/Tiles/room.tilemap.json")
write_json(p, {"format": 1, "tileset": TEX_TILES, "tileSize": 32, "width": W, "height": H, "tiles": tiles, "solid": [0, 1, 3]})
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
    obj("TitleInit", comps=[comp("TitleScreen")]),
]
write_json(os.path.join(root, "Assets/Scenes/Title.scene"), {"format": 1, "guid": SCENE_TITLE, "objects": title})
meta(os.path.join(root, "Assets/Scenes/Title.scene"), SCENE_TITLE)

# Room
coins_at = [(136, 296), (230, 392), (300, 392), (456, 264), (520, 232)]  # collected in this order
room = [
    obj("Tilemap", comps=[comp("Tilemap", map=MAP_ROOM), comp("TilemapCollider", solidOutside=True)]),
    obj("Player", pos=(34, 384, 0), tag="Player", comps=[
        comp("SpriteRenderer", texture=TEX_PLAYER, srcX=0, srcY=0, srcW=32, srcH=32, sortingOrder=5),
        comp("SpriteAnimator", clip=ANIM_IDLE),
        comp("BoxCollider2D", offsetX=6, offsetY=4, sizeX=20, sizeY=28),
        comp("PlayerController2D")]),
    obj("HudCoins", comps=[comp("UIText", x=16, y=40, color=[250, 210, 40, 255])]),
    obj("HudBest", comps=[comp("UIText", x=16, y=52, color=[200, 200, 200, 255])]),
    obj("HudMessage", comps=[comp("UIText", x=200, y=210, color=white, sortingOrder=5)]),
    obj("HudPanel", comps=[comp("UIImage", x=8, y=34, w=152, h=32, color=[0, 0, 0, 140], sortingOrder=-2)]),
    obj("Hud", comps=[comp("HudController")]),
]
for i, (cx, cy) in enumerate(coins_at):
    room.append(obj("Coin%d" % (i + 1), pos=(cx, cy, 0), tag="Coin", comps=[
        comp("SpriteRenderer", texture=TEX_COIN, srcX=0, srcY=0, srcW=16, srcH=16, sortingOrder=3),
        comp("SpriteAnimator", clip=ANIM_COIN),
        comp("BoxCollider2D", sizeX=16, sizeY=16, isTrigger=True),
        comp("Coin")]))
write_json(os.path.join(root, "Assets/Scenes/Room.scene"), {"format": 1, "guid": SCENE_ROOM, "objects": room})
meta(os.path.join(root, "Assets/Scenes/Room.scene"), SCENE_ROOM)

# ---- project settings -------------------------------------------------------------------------------------------
write_json(os.path.join(root, "ProjectSettings/Project.json"), {"format": 1, "name": "Platformer2D", "target": "ps2", "scenes": [SCENE_TITLE, SCENE_ROOM]})
write_json(os.path.join(root, "ProjectSettings/ConsoleTarget.json"), {
    "format": 1, "profile": "ps2", "scalar": "float", "resolution": [640, 448],
    "budgets": {"ramBytes": 33554432, "vramBytes": 4194304, "trianglesPerFrame": 100000, "maxObjects": 256, "maxComponentsPerType": 256},
    "textureFormats": ["clut4", "clut8", "rgba8"], "depth": "zbuffer", "media": "iso", "refreshHz": 60,
    "allowedComponents": ["SpriteRenderer", "SpriteAnimator", "Tilemap", "TilemapCollider", "BoxCollider2D", "UIText", "UIImage", "UIButton",
                          "PlayerController2D", "Coin", "HudController", "TitleMenu", "TitleScreen"]})
write_json(os.path.join(root, "ProjectSettings/InputActions.json"), {"format": 1, "actions": [
    {"name": "Move", "kind": "axis2", "source": "nub"},
    {"name": "Pad", "kind": "axis2", "source": "dpad"},
    {"name": "Jump", "kind": "button", "source": "south"}]})
print("platformer2d sample data written to", root)
