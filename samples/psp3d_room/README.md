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
