# psp3d_room

PSP-profile 3D sample for F5: a lit room, a skinned blocky character walking in place near the camera, fog,
and a second scene with 12 point lights. Scripts (camera controller and so on) are added by Stage 2.

## What is generated

Everything listed here comes from one script, with no third-party art:

- `Assets/Models/blocky_walk.{gltf,bin}`, `Assets/Textures/blocky_walk.png`: 13 boxes, 11 joints, clips `walk` (1 s, 30 fps) and `idle` (2 s).
- `Assets/Models/room.{gltf,bin}`, `Assets/Textures/room.png`: 16x16 m floor in 1 m cells, four walls, four pillars, checker texture.
- `Assets/Materials/*.mat.json`, `Assets/Scenes/{Room,Lights12}.scene`, all `.meta` files, `Assets/Primitives/{cube,plane}`, `ProjectSettings/*`.
- The same glTF files are written to `tests/fixtures/gltf/`.

## Regenerate

```sh
python3 tools/samples/gen_blocky.py --out tests/fixtures/gltf --sample samples/psp3d_room
python3 tools/samples/gen_blocky.py --check                      # regenerate in a temp dir and diff
python3 tools/samples/gen_blocky.py --preview build/f5-s0/preview.png   # silhouettes, walk frames 0 and 15
```

The output is byte-identical on every run. Hand edits to generated files are overwritten; add new hand-written
files (scripts) next to them instead.

## Cook (A1, F5.14 cook side)

```sh
build/host/tools/cooker/rensga-cooker cook --project samples/psp3d_room --target psp --force     # EBOOT form: C16 vertices
build/host/tools/cooker/rensga-cooker cook --project samples/psp3d_room --target linux --force   # float vertices
```

It cooks with a cooker built with no project scripts (the scenes use only built-in components). Both scenes share one
pack, so object GUIDs are unique across `Room` (`0x0..`) and `Lights12` (`0x1..`). `Animation.clip` is the glTF GUID
(its first animation, `walk`); `idle` is that GUID with its last byte XORed by 1. Cooking `Lights12` warns that most
objects are reached by more than 4 lights: that is the point of the scene.
