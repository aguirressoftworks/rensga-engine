# Rensga pack format v2

Source of truth: `engine/core/include/rensga/core/pack_format.h`. Writer: `tools/cooker` (`cookProject`). Reader: `PackView::open` and `World::loadPack` in `engine/core/src/pack.cpp`.

A pack is one file (`game.rpak`) that the runtime reads into 16-byte aligned memory and maps with no parsing. All values are little-endian. `Scalar` fields use the profile's scalar type (float on PSP; 20.12 fixed is reserved).

## Layout

```
offset 0    PackHeader (32 bytes)
offset 32   PackSection[sectionCount] (16 bytes each)
...         sections, each starting on a 16-byte boundary, in table order
```

`PackHeader`: `char magic[4] = "RPAK"`, `u32 version = 2`, `u32 sectionCount`, `u32 alignment` (16), `u32 totalSize`, `u32 flags`, `u32 reserved[2]`.

`flags` bit 0 (`kPackFlagSectorAligned`): every section starts on a 2048-byte CD sector and `alignment` is 2048. The cooker sets it when the ConsoleTarget has `"media": "iso"|"disc"|"cd"|"dvd"` or `"packSectorAligned": true`. The pack still has to be loaded into 16-byte aligned memory.

`PackSection`: `u32 fourcc`, `u32 offset` (from pack start, multiple of 16), `u32 size` (bytes), `u32 count` (records; for STRS, the number of strings).

Readers accept versions 1 and 2 and refuse any other `version` (and bad magic, truncation, misaligned or out-of-range sections) with a log line that names the file, for example `pack: game.rpak: unsupported pack version 3 (this runtime reads versions 1 to 2)`. Pass the file name to `PackView::open(data, size, name)`.

Version 2 adds the `tag` field to `OBJS` and the asset sections below. A v1 pack has no `tag` (the field reads 0) and no asset sections; `PackView::assets()` then returns empty tables. For v2 packs `open` also checks that every asset record's payload range lies inside `BLOB`.

## Sections

| FourCC | Record | Contents |
|---|---|---|
| `STRS` | NUL-terminated strings | String table. References are byte offsets; offset 0 is the empty string. |
| `OBJS` | `PackObject` 32 B | `Guid guid`, `u32 nameStr`, `i32 parentIndex` (-1 = root), `u16 active`, `u16 layer`, `u16 tag` (v2: index into the STRS strings, 0 = Untagged), `u16 pad`. |
| `XFRM` | `PackTransform` 48 B | Local `position[3]`, pad, `rotation[4]` (x y z w), `scale[3]`, pad. Parallel to OBJS. |
| `COMP` | `PackComponent` 16 B | `u32 typeHash` (FNV-1a 32 of the type name), `u32 objectIndex`, `u32 fieldOffset`, `u32 fieldSize`. |
| `FDAT` | blobs | Reflected field blobs; `fieldOffset` is relative to the FDAT section start. |
| `MESH` | `PackMeshRec` 16 B | `u32 vertexOffset` (vertex index into VERT), `u32 vertexCount`, `u32 primitiveType` (0 = triangle list), `u32 objectIndex` (owner of the MeshRenderer). |
| `VERT` | `PackVertex` 16 B | GU vertices `{u32 color (ABGR); float x, y, z}` = `GU_COLOR_8888 \| GU_VERTEX_32BITF`. Directional light is baked into `color`. |
| `INPT` | `PackInputAction` 16 B | `u32 nameStr`, `u32 kind` (0 button, 1 axis2), `u32 source` (nub, d-pad, face and shoulder buttons), reserved. |
| `CTGT` | `PackConsoleTarget` 48 B | One record: profile fourcc (`'PSP '`), scalar type (0 float, 1 fixed reserved), width, height, RAM and VRAM bytes, triangles per frame, max objects, max components per type. |

| `BLOB` | bytes | v2 payloads: pixels, palettes, PCM, tiles, frame rects. Every payload starts on a 16-byte boundary inside it. Records below hold byte offsets from the start of `BLOB`. |
| `TEXR` | `PackTexRec` 48 B | `Guid`, `width`, `height`, `format` (0 RGBA8, 1 CLUT4, 2 CLUT8), `pixelOffset`, `pixelBytes`, `paletteOffset`, `paletteCount` (0, 16 or 256). |
| `AUDI` | `PackAudioRec` 48 B | `Guid`, `sampleRate`, `channels` (1 or 2), `frameCount`, `loopStart`, `loopEnd` (frames), `dataOffset`, `loops`. Interleaved signed 16-bit PCM. |
| `TMAP` | `PackTilemapRec` 48 B | `Guid`, `width`, `height` (tiles), `tileSize` (px), `tilesetTexture` (TEXR index), `tilesOffset` (u16 per tile, row-major; 65535 = empty), `solidOffset`, `solidCount` (u8 per tileset tile). |
| `ANIM` | `PackAnimRec` 48 B | `Guid`, `nameStr`, `texture` (TEXR index), `frameCount`, `framesOffset` (`PackFrameRect` {u16 x, y, w, h}), `fps` (integer), `loop`. One record per clip. |
| `FONT` | `PackFontRec` 160 B | `Guid`, `texture` (TEXR index), `glyphW`, `glyphH`, `firstChar`, `charCount` (at most 128), `columns`, `lineHeight`, `advance[128]`. Self-contained, so `IRenderer::drawText` takes it by reference. Font 0 of every pack is the cooker's built-in 8x8 ASCII font (`00000000-0000-4000-8000-00000000f0f1`, texture `...f0f0`, 95 glyphs from char 32). |

### Pixel and palette layout

RGBA8 is `width*height*4` bytes, R G B A. CLUT8 is one index byte per pixel. CLUT4 packs two indices per byte, low nibble first, with each row padded to a whole byte. The palette is `paletteCount` little-endian `u32` entries with R in the low byte (the same byte order as RGBA8). Tile `i` of a tileset is row-major in its texture. Assets are found by GUID with `PackAssets::findTexture/findAudio/findTilemap/findAnim/findFont`.

### Target formats and import settings

Textures: PC targets (`linux`, `windows`, `android`) and PSP cook RGBA8. PS2 cooks CLUT4 when the image has at most 16 unique RGBA colors, else CLUT8; above 256 colors a deterministic median cut (box split on the widest channel at the weighted median, nearest-color mapping) reduces it. `.meta` `importSettings.format` can be `auto`, `rgba8`, `clut4` or `clut8` (PS2). Audio: `mono`, `halveRate` (average frame pairs, rate / 2), `loop`, `loopStart`, `loopEnd` (source frames); PS2 also honors `smallerAudio` (= mono + halveRate). Sources: PNG (stb_image), WAV (PCM 8 or 16 bit, mono or stereo), glTF 2.0 / GLB static meshes (positions, normals, COLOR_0 or material base color; no textures yet), `*.tilemap.json`, `*.anim.json`, `*.font.json` (all `"format": 1`, textures referenced by GUID).

A platform target (`rensga-cooker cook --target linux`) shares the project's console profile (`ConsoleTarget.json`) for meshes and budgets and writes `Library/Cooked/linux/game.rpak`. Texture bytes are checked against `vramBytes`.

### Hot reload (desktop)

`PackWatcher` (`engine/core/include/rensga/core/pack_watcher.h`, not compiled for PSP or PS2) polls the pack's mtime and size and reports a change once. The cooker writes the pack through a temporary file and renames it, so a changed stamp is always a complete pack.

### FDAT field blobs

Each component's blob holds its type's reflected fields in declaration order, raw, with no padding between fields. Sizes: Scalar 4, I32 4, Bool 1, Vector3 12, Quaternion 16, Color32 4 (r, g, b, a), Guid 16. `World::loadPack` rejects a blob whose size differs from the registered type's blob size (a cook/runtime mismatch). Blobs start 4-byte aligned inside FDAT.

### Cooker output order

STRS, OBJS, XFRM, COMP, FDAT, MESH, VERT, INPT, CTGT, BLOB, TEXR, AUDI, TMAP, ANIM, FONT. Objects keep scene-file order; a parent may follow its child (the loader creates all objects first, then links parents).
| `SCNE` | `PackSceneRec` 32 B | F2.05 scene list in build-settings order: `Guid`, `nameStr` (file name without extension), `firstObject`, `objectCount`, reserved. OBJS, XFRM and COMP hold the objects of every scene, one contiguous block per scene; `objectIndex` and `parentIndex` stay global. A pack without SCNE is one scene. |

## Loading

1. `PackView::open(data, size)` validates magic, version, bounds and alignment, and exposes the sections.
2. `World::loadPack(view, &loaded, sceneIndex = 0)` sizes component pools (for the largest scene when SCNE lists several) from the COMP counts, creates objects, links parents, constructs components through `TypeRegistry` (by `typeHash`) and reads their FDAT blobs, then binds `MeshRenderer::meshIndex` from MESH. It creates only the objects of scene `sceneIndex`; `SceneManager::LoadScene` calls it again at the end of a frame after destroying the old objects. `LoadedPack` returns the mesh table, input actions, the target summary and `assets` (`PackAssets`, the v2 tables).

Cooking is deterministic: the same sources and ConsoleTarget produce a byte-identical pack. The cooker keeps `Library/Cooked/<target>/cook.manifest.json` (content hashes of every input plus the reflected field layout) and skips the cook when nothing changed.
