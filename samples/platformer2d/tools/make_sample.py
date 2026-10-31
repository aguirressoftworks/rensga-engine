#!/usr/bin/env python3
"""Regenerates the platformer2d sample: PNG art, .meta files, three tilemaps, clips, scenes, audio and the project settings.
Stdlib only and deterministic (the same bytes on every machine). The scripts are C++ in Assets/Scripts and are not generated.

  python3 samples/platformer2d/tools/make_sample.py                          # the PS2 project, written into samples/platformer2d (committed)
  python3 samples/platformer2d/tools/make_sample.py --profile psp --out DIR  # the same game for the PSP profile (480x272) in DIR

The two outputs differ ONLY in ProjectSettings (Project.json target and ConsoleTarget.json): every scene uses UI anchors
(UIText / UIImage / UIButton `anchor`), so the same scenes fit 640x448 and 480x272. For --out DIR the scripts and the other
hand-written files are copied from samples/platformer2d so DIR is a complete project."""
import argparse, json, os, shutil, struct, sys, zlib

here = os.path.dirname(os.path.abspath(__file__))
sample_root = os.path.dirname(here)
ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
ap.add_argument("--profile", choices=("ps2", "psp"), default="ps2", help="ConsoleTarget profile of the generated project (default ps2)")
ap.add_argument("--out", default=None, help="output project directory (default: samples/platformer2d itself)")
args = ap.parse_args()
root = os.path.abspath(args.out) if args.out else sample_root
PROFILE = args.profile
sys.path.insert(0, here)


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
MAP_ROOM, MAP_ROOM2, MAP_ROOM3 = guid(0x111), guid(0x112), guid(0x113)
ANIM_IDLE, ANIM_RUN, ANIM_JUMP, ANIM_COIN = guid(0x121), guid(0x122), guid(0x123), guid(0x124)
ANIM_WALK, ANIM_HOP_IDLE, ANIM_HOP_AIR, ANIM_FLAG_OFF, ANIM_FLAG_ON = guid(0x125), guid(0x126), guid(0x127), guid(0x128), guid(0x129)
ANIM_DOOR_CLOSED, ANIM_DOOR_OPEN, ANIM_GOAL_OFF, ANIM_GOAL_ON = guid(0x12A), guid(0x12B), guid(0x12C), guid(0x12D)
SFX = {"jump": 0x201, "coin": 0x202, "hit": 0x203, "save": 0x204, "lose": 0x205, "clear": 0x206, "select": 0x207, "over": 0x208}
SCENE_TITLE, SCENE_ROOM, SCENE_ROOM2, SCENE_INPUT = guid(0x301), guid(0x302), guid(0x303), guid(0x304)   # 0x301..0x304 as in F2/F3
SCENE_ROOM3, SCENE_OVER, SCENE_RESULTS, SCENE_TITLEBOT, SCENE_ROOMBOT = guid(0x305), guid(0x306), guid(0x307), guid(0x308), guid(0x309)

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


# ---- props sheet: 6 cells of 32x32 (save point flag off / on, exit door closed / open, goal flag off / on) ---------------------------------
GREY, GREY_D = (150, 150, 160, 255), (100, 100, 110, 255)
GOLD2, GOLD2_D = (250, 210, 40, 255), (200, 140, 20, 255)
DOOR, DOOR_D, DOOR_IN, DOOR_LIT = (150, 100, 60, 255), (100, 65, 35, 255), (30, 25, 40, 255), (250, 235, 150, 255)
CHK_A, CHK_B, RED = (250, 250, 250, 255), (30, 30, 30, 255), (220, 60, 60, 255)


def flag_cell(on):
    img = blank()
    rect(img, 8, 2, 11, 32, GREY_D)                  # pole
    rect(img, 11, 4, 26, 14, GOLD2 if on else GREY)  # flag
    rect(img, 11, 12, 26, 14, GOLD2_D if on else GREY_D)
    rect(img, 5, 29, 15, 32, GREY_D)                 # base
    return img


def door_cell(open_):
    img = blank()
    rect(img, 3, 0, 29, 32, DOOR_D)                  # frame
    rect(img, 5, 2, 27, 32, DOOR)
    rect(img, 8, 5, 24, 32, DOOR_LIT if open_ else DOOR_IN)   # opening: lit when open
    if not open_:
        rect(img, 15, 5, 17, 32, DOOR_D)             # two leaves meet in the middle
        rect(img, 20, 18, 22, 21, GOLD2)             # handle
    return img


def goal_cell(on):
    img = blank()
    rect(img, 6, 0, 9, 32, GREY_D)                   # tall pole
    for yy in range(2, 18, 4):                       # checkered banner
        for xx in range(9, 29, 4):
            rect(img, xx, yy, xx + 4, yy + 4, CHK_A if ((xx - 9) // 4 + (yy - 2) // 4) % 2 == 0 else (CHK_B if not on else RED))
    rect(img, 3, 29, 13, 32, GREY_D)
    return img


cells = [flag_cell(False), flag_cell(True), door_cell(False), door_cell(True), goal_cell(False), goal_cell(True)]
px = []
for y in range(32):
    for cell in cells:
        px.extend(cell[y])
png(os.path.join(root, "Assets/Textures/props.png"), 192, 32, px)
meta(os.path.join(root, "Assets/Textures/props.png"), TEX_PROPS)

# ---- clips ----------------------------------------------------------------------------------------------------------
for name, g, tex, start, count, fps, cw in [("player_idle", ANIM_IDLE, TEX_PLAYER, 0, 2, 2, 32), ("player_run", ANIM_RUN, TEX_PLAYER, 4, 4, 10, 32),
                                            ("player_jump", ANIM_JUMP, TEX_PLAYER, 2, 1, 1, 32), ("coin_spin", ANIM_COIN, TEX_COIN, 0, 4, 8, 16),
                                            ("enemy_walk", ANIM_WALK, TEX_ENEMY, 0, 2, 4, 32), ("hopper_idle", ANIM_HOP_IDLE, TEX_ENEMY, 2, 1, 1, 32),
                                            ("hopper_air", ANIM_HOP_AIR, TEX_ENEMY, 3, 1, 1, 32), ("flag_off", ANIM_FLAG_OFF, TEX_PROPS, 0, 1, 1, 32),
                                            ("flag_on", ANIM_FLAG_ON, TEX_PROPS, 1, 1, 1, 32), ("door_closed", ANIM_DOOR_CLOSED, TEX_PROPS, 2, 1, 1, 32),
                                            ("door_open", ANIM_DOOR_OPEN, TEX_PROPS, 3, 1, 1, 32), ("goal_off", ANIM_GOAL_OFF, TEX_PROPS, 4, 1, 1, 32),
                                            ("goal_on", ANIM_GOAL_ON, TEX_PROPS, 5, 1, 1, 32)]:
    p = os.path.join(root, "Assets/Tiles/%s.anim.json" % name)
    write_json(p, {"format": 1, "name": name, "texture": tex, "fps": fps, "loop": True,
                   "grid": {"cellW": cw, "cellH": cw, "start": start, "count": count}})
    meta(p, g)

# ---- audio (gen_sfx.py): packed effects + the streamed music loop ------------------------------------------------------------------------------
import gen_sfx
_stdout = sys.stdout
sys.stdout = open(os.devnull, "w")
gen_sfx.generate(root)
sys.stdout = _stdout
for name, n in SFX.items():
    meta(os.path.join(root, "Assets/Audio/%s.wav" % name), guid(n))

# ---- room tilemaps (32 px tiles, 14 rows = 448 px; the Camera2D scrolls with the player) ------------------------------------------------------
BRICK_T, GRASS_T, SKY_T, WOOD_T, SLOPE_R, SLOPE_L, PLANK = 0, 1, 2, 3, 4, 5, 6


class Level:
    def __init__(self, w, h=14):
        self.w, self.h = w, h
        self.tiles = [SKY_T] * (w * h)
        for x in range(w):
            self.put(x, 0, BRICK_T)                     # ceiling
            self.put(x, h - 1, GRASS_T)                 # floor
        for y in range(h):
            self.put(0, y, BRICK_T)
            self.put(w - 1, y, BRICK_T)

    def put(self, c, r, t):
        self.tiles[r * self.w + c] = t

    def hill(self, c0):
        """A two-step slope run up to a plateau of height 2 (surface y = 352) on columns c0+2..c0+4, one slope down, then a ledge."""
        self.put(c0, 12, SLOPE_R); self.put(c0 + 1, 12, BRICK_T); self.put(c0 + 1, 11, SLOPE_R)
        for c in (c0 + 2, c0 + 3, c0 + 4):
            self.put(c, 11, WOOD_T); self.put(c, 12, BRICK_T)
        self.put(c0 + 5, 12, BRICK_T); self.put(c0 + 5, 11, SLOPE_L)

    def plank(self, c0, c1, row):
        for c in range(c0, c1 + 1):
            self.put(c, row, PLANK)

    def block(self, c, height):
        """A column of wood/brick `height` tiles tall standing on the floor."""
        for r in range(self.h - 1 - height, self.h - 1):
            self.put(c, r, WOOD_T if r == self.h - 1 - height else BRICK_T)

    def save(self, name, g):
        p = os.path.join(root, "Assets/Tiles/%s.tilemap.json" % name)
        write_json(p, {"format": 1, "tileset": TEX_TILES, "tileSize": 32, "width": self.w, "height": self.h, "tiles": self.tiles,
                       "solid": [0, 1, 3], "oneWay": [PLANK], "slopes": {str(SLOPE_R): "45R", str(SLOPE_L): "45L"}})
        meta(p, g)


# Room 1 (40 wide): the F2 room. A slope run, two one-way planks, a walker, a hopper, a checkpoint, stairs up to the exit door.
L1 = Level(40)
L1.hill(8)
L1.plank(17, 20, 10)       # plank A: surface y = 320
L1.plank(22, 25, 8)        # plank B: surface y = 256
for c, h in {34: 1, 35: 2, 36: 3, 37: 3, 38: 3}.items():
    L1.block(c, h)
L1.save("room", MAP_ROOM)

# Room 2 (48 wide): slopes, a staircase of one-way planks (A 320, B 256, C 192) and two hoppers.
L2 = Level(48)
L2.hill(5)
L2.plank(18, 21, 10)
L2.plank(23, 26, 8)
L2.plank(28, 31, 6)
L2.hill(36)
L2.save("room2", MAP_ROOM2)

# Room 3 (50 wide): the enemy gauntlet. A pillar, one plank, three walkers and two hoppers on a long floor, stairs up to the goal flag.
L3 = Level(50)
L3.block(14, 2)
L3.block(15, 2)
L3.plank(17, 20, 10)
for c, h in {44: 1, 45: 2, 46: 3, 47: 3, 48: 3}.items():
    L3.block(c, h)
L3.save("room3", MAP_ROOM3)

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


def write_scene(name, g, objects):
    p = os.path.join(root, "Assets/Scenes/%s.scene" % name)
    write_json(p, {"format": 1, "guid": g, "objects": objects})
    meta(p, g)


white = [255, 255, 255, 255]
gray = [200, 200, 200, 255]
gold = [250, 210, 40, 255]
# UI anchors (UIAnchor): every x / y below is an offset from the anchor point of the view, so one scene fits 640x448 and 480x272.
TL, TOP, TR, LEFT, CEN, RIGHT, BL, BOT, BR = range(9)


def text(name, anchor, x, y, color=white, order=0):
    return obj(name, comps=[comp("UIText", anchor=anchor, x=x, y=y, color=color, sortingOrder=order)])


def panel(name, anchor, x, y, w, h, color, order=-2):
    return obj(name, comps=[comp("UIImage", anchor=anchor, x=x, y=y, w=w, h=h, color=color, sortingOrder=order)])


def button(name, label, y, action, w=176, h=24, anchor=CEN):
    """A UIButton plus its label (the label sits on the button centre; MenuAction picks what a press does)."""
    return [obj(name, comps=[comp("UIButton", anchor=anchor, x=0, y=y, w=w, h=h, sortingOrder=5), comp("MenuAction", action=action)]),
            text(label, anchor, 0, y, white, 6)]


def backdrop(color):
    return panel("Backdrop", CEN, 0, 0, 1280, 1024, color, -10)


def title_objects(base):
    _oid[0] = base
    objs = [backdrop([40, 60, 110, 255]),
            panel("TitleBand", TOP, 0, 28, 260, 32, [20, 30, 70, 255], -9),
            text("TitleText", TOP, 0, 40, gold),
            ]
    objs += button("NewButton", "NewLabel", -6, 0)
    objs += button("ContinueButton", "ContinueLabel", 26, 1)
    objs += [text("TitleBest", BOT, 0, -34, gray), text("TitleHint", BOT, 0, -16, gray),
             obj("TitleInit", comps=[comp("TitleScreen")])]
    return objs


write_scene("Title", SCENE_TITLE, title_objects(0x200))
write_scene("TitleBot", SCENE_TITLEBOT, title_objects(0xA00) + [obj("AutoPilot", comps=[comp("AutoPilot")])])

# Game over / results
_oid[0] = 0x800
over = [backdrop([70, 20, 30, 255]), panel("OverBand", TOP, 0, 38, 200, 32, [40, 10, 15, 255], -9), text("OverText", TOP, 0, 50, [255, 120, 120, 255]),
        text("OverRoom", CEN, 0, -34, white), text("OverCoins", CEN, 0, -20, gold), text("OverTime", CEN, 0, -6, gray)]
over += button("RetryButton", "RetryLabel", 26, 4)
over += button("QuitButton", "QuitLabel", 58, 3)
over.append(obj("OverInit", comps=[comp("GameOverScreen")]))
write_scene("GameOver", SCENE_OVER, over)

_oid[0] = 0x900
res = [backdrop([30, 80, 70, 255]), panel("ResBand", TOP, 0, 38, 220, 32, [15, 50, 45, 255], -9), text("ResText", TOP, 0, 50, gold),
       text("ResTime", CEN, 0, -30, white), text("ResCoins", CEN, 0, -16, gold), text("ResBest", CEN, 0, -2, gray)]
res += button("AgainButton", "AgainLabel", 30, 0)
res += button("MenuButton", "MenuLabel", 62, 3)
res.append(obj("ResInit", comps=[comp("ResultsScreen")]))
write_scene("Results", SCENE_RESULTS, res)

# ---- rooms ----------------------------------------------------------------------------------------------------------
LAYER_PLAYER, LAYER_ENEMY, LAYER_PICKUP = 1, 2, 3


def hud_objects(room):
    objs = [panel("HudPanelL", TL, 4, 4, 112, 28, [0, 0, 0, 140]), panel("HudPanelC", TOP, 0, 4, 112, 28, [0, 0, 0, 140]),
            panel("HudPanelR", TR, -4, 4, 112, 28, [0, 0, 0, 140]),
            text("HudCoins", TL, 10, 9, gold), text("HudRoom", TL, 10, 21, gray),
            text("HudLives", TOP, 0, 9, white), text("HudHp", TOP, 0, 21, [255, 120, 120, 255]),
            text("HudTime", TR, -10, 9, white),
            text("HudMessage", CEN, 0, -56, white, 5),
            obj("Hud", comps=[comp("HudController", roomNumber=room)])]
    # pause menu: shown by PauseMenu while paused (the parent is inactive otherwise)
    ui = obj("PauseUI")
    objs.append(ui)
    objs.append(obj("Pause", comps=[comp("PauseMenu")]))
    pui = ui["guid"]
    kids = [obj("PausePanel", comps=[comp("UIImage", anchor=CEN, x=0, y=0, w=220, h=128, color=[10, 12, 30, 220], sortingOrder=20)], parent=pui),
            obj("PauseTitle", comps=[comp("UIText", anchor=CEN, x=0, y=-44, color=gold, sortingOrder=22)], parent=pui),
            obj("ResumeButton", comps=[comp("UIButton", anchor=CEN, x=0, y=-12, w=160, h=24, sortingOrder=21), comp("MenuAction", action=2)], parent=pui),
            obj("ResumeLabel", comps=[comp("UIText", anchor=CEN, x=0, y=-12, color=white, sortingOrder=22)], parent=pui),
            obj("QuitButton", comps=[comp("UIButton", anchor=CEN, x=0, y=22, w=160, h=24, sortingOrder=21), comp("MenuAction", action=3)], parent=pui),
            obj("QuitLabel", comps=[comp("UIText", anchor=CEN, x=0, y=22, color=white, sortingOrder=22)], parent=pui)]
    return objs + kids


def build_room(room, level, map_guid, start, coins, savepoint, enemies, exit_pos, goal, base=None):
    """room: 1..3. coins: [(x, y)] in collection order. enemies: [(name, kind, x, patrolMin, patrolMax)]."""
    _oid[0] = base if base else {1: 0x400, 2: 0x500, 3: 0x700}[room]
    objs = [obj("Tilemap", comps=[comp("Tilemap", map=map_guid), comp("TilemapCollider2D", solidOutside=True)])]
    player = obj("Player", pos=(start[0], start[1], 0), tag="Player", comps=[
        comp("SpriteRenderer", texture=TEX_PLAYER, srcX=0, srcY=0, srcW=32, srcH=32, sortingOrder=5),
        comp("SpriteAnimator", clip=ANIM_IDLE),
        comp("BoxCollider2D", offsetX=6, offsetY=4, sizeX=20, sizeY=28),
        comp("PlayerController2D")])
    player["layer"] = LAYER_PLAYER
    objs.append(player)
    objs.append(obj("Camera", comps=[comp("Camera2D", target=player["guid"], targetOffsetX=16, targetOffsetY=16, deadZoneW=64, deadZoneH=48,
                                          useBounds=True, boundsX=0, boundsY=0, boundsW=level.w * 32, boundsH=level.h * 32, pixelSnap=True)]))
    objs += hud_objects(room)
    for i, (cx, cy) in enumerate(coins):
        c = obj("Coin%d" % (i + 1), pos=(cx, cy, 0), tag="Coin", comps=[
            comp("SpriteRenderer", texture=TEX_COIN, srcX=0, srcY=0, srcW=16, srcH=16, sortingOrder=3),
            comp("SpriteAnimator", clip=ANIM_COIN),
            comp("BoxCollider2D", sizeX=16, sizeY=16, isTrigger=True),
            comp("Coin")])
        c["layer"] = LAYER_PICKUP
        objs.append(c)
    sp = obj("SavePoint", pos=(savepoint, 384, 0), tag="SavePoint", comps=[
        comp("SpriteRenderer", texture=TEX_PROPS, srcX=0, srcY=0, srcW=32, srcH=32, sortingOrder=2),
        comp("SpriteAnimator", clip=ANIM_FLAG_OFF),
        comp("BoxCollider2D", offsetX=8, offsetY=0, sizeX=16, sizeY=32, isTrigger=True),
        comp("SavePoint")])
    sp["layer"] = LAYER_PICKUP
    objs.append(sp)
    for name, kind, ex, pmin, pmax in enemies:
        behaviour = comp("Walker", patrolMinX=pmin, patrolMaxX=pmax) if kind == "walker" else comp("Hopper", patrolMinX=pmin, patrolMaxX=pmax)
        e = obj(name, pos=(ex, 384, 0), tag="Enemy", comps=[
            comp("SpriteRenderer", texture=TEX_ENEMY, srcX=0, srcY=0, srcW=32, srcH=32, sortingOrder=4),
            comp("SpriteAnimator", clip=ANIM_WALK if kind == "walker" else ANIM_HOP_IDLE),
            comp("BoxCollider2D", offsetX=4, offsetY=12, sizeX=24, sizeY=20),
            behaviour])
        e["layer"] = LAYER_ENEMY
        objs.append(e)
    ex, ey = exit_pos
    door = obj("Goal" if goal else "Exit", pos=(ex, ey, 0), tag="Exit", comps=[
        comp("SpriteRenderer", texture=TEX_PROPS, srcX=0, srcY=0, srcW=32, srcH=32, sortingOrder=2),
        comp("SpriteAnimator", clip=ANIM_GOAL_OFF if goal else ANIM_DOOR_CLOSED),
        comp("BoxCollider2D", offsetX=-8, offsetY=0, sizeX=48, sizeY=32, isTrigger=True),
        comp("RoomExit", isGoal=goal)])
    door["layer"] = LAYER_PICKUP
    objs.append(door)
    return objs


# Room 1: coins in collection order (the pilot's plan, see AutoPilot.cpp).
write_scene("Room", SCENE_ROOM, build_room(
    1, L1, MAP_ROOM, (64, 384),
    [(168, 392), (360, 328), (600, 296), (760, 232), (846, 392), (1000, 392), (1192, 296)], 880,
    [("Walker1", "walker", 560, 480, 736), ("Hopper1", "hopper", 1000, 928, 1080)], (1216, 288), False))
# RoomBot: room 1 plus an AutoPilot (it survives the scene loads and plays on through rooms 2 and 3 to the results): the quick way into the
# game for automated runs that skip the title.
write_scene("RoomBot", SCENE_ROOMBOT, build_room(
    1, L1, MAP_ROOM, (64, 384),
    [(168, 392), (360, 328), (600, 296), (760, 232), (846, 392), (1000, 392), (1192, 296)], 880,
    [("Walker1", "walker", 560, 480, 736), ("Hopper1", "hopper", 1000, 928, 1080)], (1216, 288), False, base=0xB00) +
    [obj("AutoPilot", comps=[comp("AutoPilot")])])
# Room 2: slopes + a plank staircase + hoppers.
write_scene("Room2", SCENE_ROOM2, build_room(
    2, L2, MAP_ROOM2, (64, 384),
    [(264, 328), (500, 392), (632, 296), (792, 232), (952, 168), (1264, 328)], 552,
    [("Hopper1", "hopper", 420, 372, 520), ("Hopper2", "hopper", 1060, 1040, 1130)], (1440, 384), False))
# Room 3: the gauntlet + the goal flag.
write_scene("Room3", SCENE_ROOM3, build_room(
    3, L3, MAP_ROOM3, (64, 384),
    [(140, 392), (472, 328), (600, 296), (1140, 392), (1450, 328), (1500, 296)], 900,
    [("Walker1", "walker", 260, 200, 400), ("Hopper1", "hopper", 760, 700, 850), ("Walker2", "walker", 1000, 960, 1100),
     ("Hopper2", "hopper", 1230, 1180, 1290), ("Walker3", "walker", 1340, 1310, 1400)], (1536, 288), True))

# InputTest (F3.06): every axis, button and key the Input API reports, plus rumble. Not part of the Title -> Room flow.
# Compact so it fits 480x272 (TopLeft anchors, rows 14 px apart); the hint hangs from the bottom edge.
_oid[0] = 0x600
inp = [backdrop([24, 28, 44, 255]),
       text("InTitle", TL, 8, 6, gold),
       text("InHint", BL, 8, -8, gray),
       text("InKeys", TL, 8, 176, white),
       text("InAnyKey", TL, 8, 190, gray),
       text("InRumble", TL, 8, 206, [255, 170, 90, 255]),
       obj("InHud", comps=[comp("InputTestHud")])]
for i in range(10):
    y = 22 + i * 14
    inp.append(text("IaL%d" % i, TL, 8, y, white))
    inp.append(panel("IaB%d" % i, TL, 172, y, 96, 8, [60, 66, 90, 255], -5))
    inp.append(panel("IaF%d" % i, TL, 172, y, 0, 8, [250, 210, 40, 255], -4))
    if i < 8:
        inp.append(panel("IaC%d" % i, TL, 219, y - 1, 2, 10, [140, 150, 180, 255], -3))
for i in range(8):
    inp.append(text("Ib%d" % i, TL, 284, 22 + i * 14, [150, 150, 150, 255]))
write_scene("InputTest", SCENE_INPUT, inp)

# ---- script metas (new scripts get a fixed GUID; the old ones keep theirs) ---------------------------------------------------------------------
if root == sample_root:
    for i, name in enumerate(("MenuAction.cpp", "PauseMenu.cpp", "RoomExit.cpp", "Screens.cpp")):
        write_json(os.path.join(root, "Assets/Scripts", name + ".meta"),
                   {"guid": "5f4a0c1e-6b1d-4c2a-9a00-%012x" % (0x3d0000000000 + i + 1), "importSettings": {}, "importer": "script"})
else:   # a generated project is complete: copy the hand-written scripts
    dst = os.path.join(root, "Assets/Scripts")
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(os.path.join(sample_root, "Assets/Scripts"), dst)

# ---- project settings -------------------------------------------------------------------------------------------
scenes = [SCENE_TITLE, SCENE_ROOM, SCENE_ROOM2, SCENE_ROOM3, SCENE_OVER, SCENE_RESULTS, SCENE_TITLEBOT, SCENE_INPUT, SCENE_ROOMBOT]
write_json(os.path.join(root, "ProjectSettings/Project.json"), {"format": 1, "name": "Platformer2D", "target": PROFILE, "scenes": scenes})
components = ["SpriteRenderer", "SpriteAnimator", "Tilemap", "TilemapCollider2D", "BoxCollider2D", "Rigidbody2D", "UIText", "UIImage", "UIButton",
              "Camera2D", "PlayerController2D", "Coin", "Walker", "Hopper", "SavePoint", "RoomExit", "AutoPilot", "HudController", "PauseMenu",
              "MenuAction", "TitleScreen", "GameOverScreen", "ResultsScreen", "InputTestHud"]
if PROFILE == "ps2":
    target = {
        "format": 1, "profile": "ps2", "scalar": "float", "resolution": [640, 448],
        "budgets": {"ramBytes": 33554432, "vramBytes": 4194304, "trianglesPerFrame": 100000, "maxObjects": 256, "maxComponentsPerType": 256},
        "textureFormats": ["clut4", "clut8", "rgba8"], "depth": "zbuffer", "media": "iso", "refreshHz": 60,
        "audio": {"sampleRate": 22050, "voices": 16},
        "capabilities": {"rumble": True},
        "allowedComponents": components}
else:   # PSP: 480x272, 16-bit framebuffer, 24 MB for the game (the PSP-1000 has 32 MB), no rumble
    target = {
        "format": 1, "profile": "psp", "scalar": "float", "resolution": [480, 272],
        "budgets": {"ramBytes": 25165824, "vramBytes": 2097152, "trianglesPerFrame": 20000, "maxObjects": 256, "maxComponentsPerType": 256},
        "textureFormats": ["clut4", "clut8", "rgba5551", "rgba4444", "rgba8"],
        "framebuffer": {"buffers": 2, "bpp": 16, "depthBits": 16},
        "depth": "zbuffer", "media": "eboot", "refreshHz": 60,
        "audio": {"sampleRate": 22050, "voices": 16},
        "allowedComponents": components}
write_json(os.path.join(root, "ProjectSettings/ConsoleTarget.json"), target)

# ---- input actions / physics ----------------------------------------------------------------------------------------------
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
