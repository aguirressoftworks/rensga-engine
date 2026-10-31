#!/usr/bin/env python3
"""Regenerates the binary assets and the JSON of this fixture (F4.08 exercise_paint_level, editor_tile_palette_ui,
editor_sprite_editor_ui). Run from anywhere. A tiny PS2-profile project: one scene with a 2D camera, a 4 x 2 tileset of
16 px tiles, a 2-frame player sheet, a rule tile and a controller script. Everything else (tilemap, objects) is made by
the editor in the tests."""
import json, os, struct, zlib
here = os.path.dirname(os.path.abspath(__file__))

def png(path, w, h, px):
    raw = b"".join(b"\x00" + bytes(c for p in px[y*w:(y+1)*w] for c in p) for y in range(h))
    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

T = (0, 0, 0, 0)
GRASS, GRASS2 = (96, 190, 70, 255), (60, 140, 50, 255)
DIRT, DIRT2 = (140, 98, 60, 255), (110, 74, 44, 255)
WOOD, WOOD2 = (200, 150, 80, 255), (150, 105, 50, 255)
STONE, STONE2 = (130, 134, 150, 255), (96, 100, 116, 255)
SPIKE = (220, 70, 60, 255)

def tile(kind):
    t = [[T] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            speck = (x * 7 + y * 13) % 11 == 0
            if kind == 0:      # grass on dirt
                t[y][x] = (GRASS2 if y == 3 else GRASS) if y < 4 else (DIRT2 if speck else DIRT)
            elif kind == 1:    # dirt
                t[y][x] = DIRT2 if speck else DIRT
            elif kind == 2:    # one-way wooden platform
                if y < 5: t[y][x] = WOOD2 if y == 4 or x % 8 == 0 else WOOD
            elif kind == 3:    # stone brick
                t[y][x] = STONE2 if (y % 8 == 7 or (x + 8 * ((y // 8) % 2)) % 16 == 15) else STONE
            elif kind == 4:    # slope 45L: rises to the left
                if x + y >= 15: t[y][x] = DIRT2 if y == 15 - x else DIRT
            elif kind == 5:    # slope 45R: rises to the right
                if y >= x: t[y][x] = DIRT2 if y == x else DIRT
            elif kind == 6:    # spikes
                if y >= 8 and (x % 8) >= (y - 8) * 0 and abs((x % 8) - 4) <= (y - 8) // 2: t[y][x] = SPIKE
            elif kind == 7:    # coin box
                if 2 <= x <= 13 and 2 <= y <= 13: t[y][x] = (250, 205, 60, 255) if (x in (2, 13) or y in (2, 13)) else (230, 160, 30, 255)
    return t

px = [T] * (64 * 32)
for k in range(8):
    tt = tile(k)
    for y in range(16):
        for x in range(16):
            px[((k // 4) * 16 + y) * 64 + (k % 4) * 16 + x] = tt[y][x]
png(os.path.join(here, "Assets/Textures/tiles.png"), 64, 32, px)

BODY, BODY2, SKIN, EYE = (232, 150, 40, 255), (190, 110, 30, 255), (250, 214, 170, 255), (30, 30, 40, 255)
def hero(frame):
    f = [[T] * 16 for _ in range(16)]
    def rect(x0, y0, x1, y1, c):
        for y in range(y0, y1):
            for x in range(x0, x1): f[y][x] = c
    rect(5, 1, 11, 7, SKIN); rect(5, 1, 11, 3, BODY2); rect(8, 3, 10, 5, EYE)
    rect(4, 7, 12, 12, BODY)
    if frame == 0: rect(5, 12, 7, 16, BODY2); rect(9, 12, 11, 16, BODY2)
    else: rect(4, 12, 7, 16, BODY2); rect(10, 12, 12, 15, BODY2)
    return f
px = [T] * (32 * 16)
for k in range(2):
    h = hero(k)
    for y in range(16):
        for x in range(16): px[y * 32 + k * 16 + x] = h[y][x]
png(os.path.join(here, "Assets/Textures/player.png"), 32, 16, px)

def write(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write(json.dumps(obj, indent=1) + "\n")

TILES_GUID = "00000000-0000-4000-8000-0000000b2d01"
SCENE_GUID = "00000000-0000-4000-8000-0000000b2d07"
write(os.path.join(here, "Assets/Textures/tiles.png.meta"), {"guid": TILES_GUID, "importSettings": {}})
write(os.path.join(here, "Assets/Scenes/Level.scene.meta"), {"guid": SCENE_GUID, "importSettings": {}})
write(os.path.join(here, "Assets/Scenes/Level.scene"), {
    "format": 1, "guid": SCENE_GUID,
    "objects": [{"guid": "00000000-0000-4000-8000-0000000b2d11", "name": "Camera2D", "parent": None, "active": True,
                 "transform": {"position": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": [1, 1, 1]},
                 "components": [{"type": "Camera2D", "fields": {"deadZoneW": 64, "deadZoneH": 48}}], "prefab": None}]})
write(os.path.join(here, "Assets/Tiles/ground.ruletile.json"), {
    "format": 1, "tileset": TILES_GUID, "neighbours": 4, "outside": "filled", "default": 1, "members": [0],
    "rules": [{"mask": 0, "care": 1, "tile": 0}]})   # no neighbour above: grass top, otherwise dirt
write(os.path.join(here, "ProjectSettings/Project.json"), {"format": 1, "name": "Paint2D", "target": "ps2", "scenes": [SCENE_GUID]})
write(os.path.join(here, "ProjectSettings/InputActions.json"), {"format": 1, "actions": [
    {"name": "Pad", "kind": "axis2", "source": "dpad"}, {"name": "Jump", "kind": "button", "source": "south"}]})
write(os.path.join(here, "ProjectSettings/ConsoleTarget.json"), {
    "format": 1, "profile": "ps2", "scalar": "float", "resolution": [640, 448],
    "budgets": {"ramBytes": 33554432, "vramBytes": 4194304, "trianglesPerFrame": 100000, "maxObjects": 256, "maxComponentsPerType": 256},
    "textureFormats": ["clut4", "clut8", "rgba8"], "depth": "zbuffer", "media": "iso", "refreshHz": 60,
    "audio": {"sampleRate": 22050, "voices": 16}, "capabilities": {"rumble": True},
    "allowedComponents": ["SpriteRenderer", "Tilemap", "TilemapCollider2D", "BoxCollider2D", "Rigidbody2D", "Camera2D", "PaintPlayer"]})
