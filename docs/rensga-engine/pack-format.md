# Rensga pack format v1

Source of truth: `engine/core/include/rensga/core/pack_format.h`. Writer: `tools/cooker` (`cookProject`). Reader: `PackView::open` and `World::loadPack` in `engine/core/src/pack.cpp`.

A pack is one file (`game.rpak`) that the runtime reads into 16-byte aligned memory and maps with no parsing. All values are little-endian. `Scalar` fields use the profile's scalar type (float on PSP; 20.12 fixed is reserved).

## Layout

```
offset 0    PackHeader (32 bytes)
offset 32   PackSection[sectionCount] (16 bytes each)
...         sections, each starting on a 16-byte boundary, in table order
```

`PackHeader`: `char magic[4] = "RPAK"`, `u32 version = 1`, `u32 sectionCount`, `u32 alignment` (16), `u32 totalSize`, `u32 flags`, `u32 reserved[2]`.

`flags` bit 0 (`kPackFlagSectorAligned`) is reserved: sections aligned to 2048-byte sectors for disc targets (alignment would then be 2048). The MVP never sets it.

`PackSection`: `u32 fourcc`, `u32 offset` (from pack start, multiple of 16), `u32 size` (bytes), `u32 count` (records; for STRS, the number of strings).

The loader refuses any other `version` (and bad magic, truncation, misaligned or out-of-range sections) with a log line, for example `pack: unsupported version 2 (this runtime reads version 1)`.

## Sections

| FourCC | Record | Contents |
|---|---|---|
| `STRS` | NUL-terminated strings | String table. References are byte offsets; offset 0 is the empty string. |
| `OBJS` | `PackObject` 32 B | `Guid guid`, `u32 nameStr`, `i32 parentIndex` (-1 = root), `u16 active`, `u16 layer`, `u32 reserved`. |
| `XFRM` | `PackTransform` 48 B | Local `position[3]`, pad, `rotation[4]` (x y z w), `scale[3]`, pad. Parallel to OBJS. |
| `COMP` | `PackComponent` 16 B | `u32 typeHash` (FNV-1a 32 of the type name), `u32 objectIndex`, `u32 fieldOffset`, `u32 fieldSize`. |
| `FDAT` | blobs | Reflected field blobs; `fieldOffset` is relative to the FDAT section start. |
| `MESH` | `PackMeshRec` 16 B | `u32 vertexOffset` (vertex index into VERT), `u32 vertexCount`, `u32 primitiveType` (0 = triangle list), `u32 objectIndex` (owner of the MeshRenderer). |
| `VERT` | `PackVertex` 16 B | GU vertices `{u32 color (ABGR); float x, y, z}` = `GU_COLOR_8888 \| GU_VERTEX_32BITF`. Directional light is baked into `color`. |
| `INPT` | `PackInputAction` 16 B | `u32 nameStr`, `u32 kind` (0 button, 1 axis2), `u32 source` (nub, d-pad, face and shoulder buttons), reserved. |
| `CTGT` | `PackConsoleTarget` 48 B | One record: profile fourcc (`'PSP '`), scalar type (0 float, 1 fixed reserved), width, height, RAM and VRAM bytes, triangles per frame, max objects, max components per type. |

### FDAT field blobs

Each component's blob holds its type's reflected fields in declaration order, raw, with no padding between fields. Sizes: Scalar 4, I32 4, Bool 1, Vector3 12, Quaternion 16, Color32 4 (r, g, b, a), Guid 16. `World::loadPack` rejects a blob whose size differs from the registered type's blob size (a cook/runtime mismatch). Blobs start 4-byte aligned inside FDAT.

### Cooker output order

STRS, OBJS, XFRM, COMP, FDAT, MESH, VERT, INPT, CTGT. Objects keep scene-file order; a parent may follow its child (the loader creates all objects first, then links parents).

## Loading

1. `PackView::open(data, size)` validates magic, version, bounds and alignment, and exposes the sections.
2. `World::loadPack(view, &loaded)` sizes component pools from the COMP counts, creates objects, links parents, constructs components through `TypeRegistry` (by `typeHash`) and reads their FDAT blobs, then binds `MeshRenderer::meshIndex` from MESH. `LoadedPack` returns the mesh table, input actions and the target summary.

Cooking is deterministic: the same sources and ConsoleTarget produce a byte-identical pack. The cooker keeps `Library/Cooked/<target>/cook.manifest.json` (content hashes of every input plus the reflected field layout) and skips the cook when nothing changed.
