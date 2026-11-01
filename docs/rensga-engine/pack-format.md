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
| `VERT` | `PackVertex` 16 B | GU vertices `{u32 color (ABGR); float x, y, z}` = `GU_COLOR_8888 \| GU_VERTEX_32BITF`. Directional light is baked into `color`. With an `MSHX` section the records are `PackVertexCompact` 12 B instead (see "Compact meshes"); `count` is then the number of compact vertices. |
| `MSHX` | `PackMeshExt` 32 B | Optional, one record, only in a pack cooked for the `psp` target itself. Says the `VERT` records are compact. See "Compact meshes". |
| `INPT` | `PackInputAction` 16 B | `u32 nameStr`, `u32 kind` (0 button, 1 axis2), `u32 source` (nub, d-pad, face and shoulder buttons), reserved. |
| `CTGT` | `PackConsoleTarget` 48 B | One record: profile fourcc (`'PSP '`), scalar type (0 float, 1 fixed reserved), width, height, RAM and VRAM bytes, triangles per frame, max objects, max components per type. |

| `BLOB` | bytes | v2 payloads: pixels, palettes, PCM, tiles, frame rects. Every payload starts on a 16-byte boundary inside it. Records below hold byte offsets from the start of `BLOB`. |
| `TEXR` | `PackTexRec` 48 B | `Guid`, `width`, `height` (the SOURCE size), `format` (0 RGBA8, 1 CLUT4, 2 CLUT8, 3 RGBA5551, 4 RGBA4444), `pixelOffset`, `pixelBytes`, `paletteOffset`, `u16 paletteCount` (0, 16 or 256), `u16 flags` (bit 0 `kTexSwizzled`), `u16 storedWidth`, `u16 storedHeight` (0 = same as `width`/`height`). Byte-compatible with the first v2 layout, where `paletteCount` was a `u32` and the last word was reserved zero. See "PSP textures". |
| `AUDI` | `PackAudioRec` 48 B | `Guid`, `sampleRate`, `channels` (1 or 2), `frameCount`, `loopStart`, `loopEnd` (frames), `dataOffset`, `loops`. Interleaved signed 16-bit PCM. |
| `TMAP` | `PackTilemapRec` 48 B | `Guid`, `width`, `height` (tiles), `tileSize` (px), `tilesetTexture` (TEXR index), `tilesOffset` (u16 per tile, row-major; 65535 = empty), `solidOffset`, `solidCount` (one flag byte per tileset tile, see "Tile flags"). |
| `ANIM` | `PackAnimRec` 48 B | `Guid`, `nameStr`, `texture` (TEXR index), `frameCount`, `framesOffset` (`PackFrameRect` {u16 x, y, w, h}), `fps` (integer), `loop`. One record per clip. |
| `FONT` | `PackFontRec` 160 B | `Guid`, `texture` (TEXR index), `glyphW`, `glyphH`, `firstChar`, `charCount` (at most 128), `columns`, `lineHeight`, `advance[128]`. Self-contained, so `IRenderer::drawText` takes it by reference. Font 0 of every pack is the cooker's built-in 8x8 ASCII font (`00000000-0000-4000-8000-00000000f0f1`, texture `...f0f0`, 95 glyphs from char 32). |

### Tile flags (TMAP)

Each tileset tile has one flag byte. The pack version does not change: a byte of 1 is a full solid block, which is how every earlier pack reads, and a reader that only tests "not zero" still treats every flagged tile as solid.

| Bits | Name | Meaning |
| --- | --- | --- |
| 0 | `kSolid` | Full block. Blocks from every side. |
| 1 | `kOneWay` | Blocks only a box that falls onto its top edge (previous bottom edge at or above the top). Moving up, sideways or standing inside it does not block. `BoxCollider2D::DropThrough` (or Down + South in `Move`) ignores it until the box has fallen past. |
| 2..4 | slope | Shape 0..6: none, `45L`, `45R`, `22L_lo`, `22L_hi`, `22R_lo`, `22R_hi`. |

Slope heights are in sixteenths of the tile, up from the tile's bottom edge, left edge to right edge. `L` is high on the left (`\`), `R` is high on the right (`/`): `45L` 16 to 0, `45R` 0 to 16, `22L_hi` 16 to 8, `22L_lo` 8 to 0, `22R_lo` 0 to 8, `22R_hi` 8 to 16. A 22.5 degree slope uses two tiles side by side (the `hi` tile on the high side). A slope supports from above only: the foot point (middle of the box's bottom edge) follows its surface while the box is grounded, and a box can run from a slope onto the level tile at its top.

Source JSON (`*.tilemap.json`): `"solid": [ids]` (unchanged), `"oneWay": [ids]`, `"slopes": {"<id>": "45R", ...}`. A tile id may have only one of the three; the cooker reports the asset otherwise. A map with only `"solid"` cooks to the same bytes as before.

### Layer collision matrix (PH2D)

One `PackPhysics2D` record (128 B): `masks[32]`, where bit `b` of `masks[a]` set means layer `a` collides with layer `b`. The section is absent when the project has no `ProjectSettings/Physics2D.json`, and then every layer collides with every other. A `GameObject`'s layer is the low 5 bits of `PackObject::layer`. `Physics2D::loadFromPack` reads it when the pack is bound; `Physics2D::IgnoreLayerCollision` changes it at run time. `BoxCollider2D::Move` skips tilemaps whose object layer does not collide with the mover's, and trigger events check the pair.

`Physics2D.json`: `{"format": 1, "layers": ["Default", "Player", "Enemy"], "ignore": [["Player", "Enemy"], [3, 4]]}`. Pairs use indices (0..31) or names from `layers`. Optional `"matrix"`: 32 bitmasks, applied before `ignore`. Pairs are made symmetric.

### Pixel and palette layout

RGBA8 is `width*height*4` bytes, R G B A. CLUT8 is one index byte per pixel. CLUT4 packs two indices per byte, low nibble first, with each row padded to a whole byte. The palette is `paletteCount` little-endian `u32` entries with R in the low byte (the same byte order as RGBA8). Tile `i` of a tileset is row-major in its texture. Assets are found by GUID with `PackAssets::findTexture/findAudio/findTilemap/findAnim/findFont`.

### PSP textures

The `psp` cooker plugin (`tools/cooker/src/psp_format.cpp`) writes every texture in the form the GU samples directly: padded to its stored size, swizzled, with an RGBA8 palette for CLUT formats.

| `importSettings.format` | `PackTexRec.format` | Pixel bytes |
|---|---|---|
| `auto` (default) | 1 or 2 | The PS2 palette code (`makeClut(img, 0)`): CLUT4 up to 16 unique RGBA colors, else CLUT8, with the deterministic median cut above 256. |
| `clut4` | 1 | 4 bits per pixel, two pixels per byte, **low nibble first** (pixel `x` even is the low nibble). 16-entry palette. |
| `clut8` | 2 | 8 bits per pixel. 256-entry palette. |
| `rgba5551` | 3 | `u16`, `GU_PSM_5551`: R bits 0-4, G bits 5-9, B bits 10-14, A bit 15 (alpha at or above 128 is 1). |
| `rgba4444` | 4 | `u16`, `GU_PSM_4444`: R bits 0-3, G bits 4-7, B bits 8-11, A bits 12-15. |
| `rgba8` | 0 | `u32` per pixel, R in the low byte (`GU_PSM_8888`). |

Palettes are `paletteCount` `u32` entries, R in the low byte, starting on a 16-byte boundary (every `BLOB` payload does). The ConsoleTarget lists them in `textureFormats`.

**Stored size.** `width` x `height` stay the source size, so the tile and clip code never sees the padding. The pixels are stored at `storedWidth` x `storedHeight`:

- each is the source edge rounded up to a power of two, at most 512;
- the stored width is at least 32 pixels for CLUT4, 16 for CLUT8 and 8 for the 16- and 32-bit formats;
- the stored height is at least 8;
- padding bytes (right of the source, below it) are zero, so a CLUT pixel in the padding is index 0.

A source whose stored size would pass 512 in either edge is a cook error that names the asset and its GUID and suggests the `maxSize` import setting (`maxSize` scales the longest edge down before the plugin sees it). `pixelBytes` is `storedWidth * storedHeight * bits / 8`. UVs for the GU are the source size over the stored size.

**Swizzle (`kTexSwizzled`).** The stored image is cut into blocks 16 bytes wide and 8 rows high. Blocks are stored in bands from top to bottom; inside a band from left to right; inside a block row by row, 16 bytes per row. A byte at row `y`, byte column `bx` of the linear stored image (pitch `storedWidth * bits / 8`) lands at `((y / 8) * (pitch / 16) + bx / 16) * 128 + (y % 8) * 16 + bx % 16`. The stored width rules above make the pitch a multiple of 16 for every format, and the stored height a multiple of 8. Only the PSP plugin sets the flag. The GL and GS renderers refuse a texture with `flags != 0`, so platform cooks (`linux`, `windows`, `android`) of a PSP project write plain RGBA8 at the source size with `flags == 0`. Per-format overrides for the PSP belong in the `.meta` `targetOverrides.psp`, because a platform target rejects a console-only format name.

### Compact meshes (MSHX)

A pack cooked for the `psp` target itself stores its mesh vertices as `PackVertexCompact` (12 B: `u32 color; i16 x, y, z; u16 pad` = `GU_COLOR_8888 | GU_VERTEX_16BIT`) and adds one `MSHX` record:

```
PackMeshExt (32 B): u32 format (1 = compact16), float offset[3], float scale[3], u32 reserved
```

A vertex position is `offset + xyz * scale`. One offset and one scale cover the whole `VERT` section. The cooker sets `offset` to the middle of the bounding box on each axis and `scale` to the smallest power of two with `halfExtent / scale <= 32767`, so the stored error is at most `scale / 2` (a 200-unit span keeps better than 4 mm). The `MESH` records' `vertexOffset` and `vertexCount` are vertex indices in both forms. A pack without `MSHX` has float vertices. The `linux`, `windows` and `android` cooks of a PSP project never write `MSHX`.

### PSP RAM ceiling

`ConsoleTarget.json` `budgets.ramBytes` for the `psp` profile may not exceed 32 MiB, the RAM of a PSP-1000. The cook reports `budget exceeded ... above the psp device limit` and writes no pack. The limit is data: `limits.ramLimitBytes` of the profile in `tools/cooker/data/profile_defaults.json` (read by `profileRamLimitBytes`), which `ConsoleTarget.json` `"limits"` can override, so another profile adds its own without code.

### Target formats and import settings

Textures: PC targets (`linux`, `windows`, `android`) cook RGBA8. The PSP has its own formats (see "PSP textures"). PS2 cooks CLUT4 when the image has at most 16 unique RGBA colors, else CLUT8; above 256 colors a deterministic median cut (box split on the widest channel at the weighted median, nearest-color mapping) reduces it. `.meta` `importSettings.format` can be `auto`, `rgba8`, `clut4` or `clut8` (PS2), and also `rgba5551` or `rgba4444` (PSP). Audio: `mono`, `halveRate` (average frame pairs, rate / 2), `loop`, `loopStart`, `loopEnd` (source frames); PS2 also honors `smallerAudio` (= mono + halveRate). Sources: PNG (stb_image), WAV (PCM 8 or 16 bit, mono or stereo), glTF 2.0 / GLB static meshes (positions, normals, COLOR_0 or material base color; no textures yet), `*.tilemap.json`, `*.anim.json`, `*.font.json` (all `"format": 1`, textures referenced by GUID).

A platform target (`rensga-cooker cook --target linux`) shares the project's console profile (`ConsoleTarget.json`) for meshes and budgets and writes `Library/Cooked/linux/game.rpak`. Texture bytes are checked against `vramBytes`.

### Hot reload (desktop)

`PackWatcher` (`engine/core/include/rensga/core/pack_watcher.h`, not compiled for PSP or PS2) polls the pack's mtime and size and reports a change once. The cooker writes the pack through a temporary file and renames it, so a changed stamp is always a complete pack.

### FDAT field blobs

Each component's blob holds its type's reflected fields in declaration order, raw, with no padding between fields. Sizes: Scalar 4, I32 4, Bool 1, Vector3 12, Quaternion 16, Color32 4 (r, g, b, a), Guid 16, UnityEvent 180. F6 adds four field types (field type numbers 8 to 11, appended to `FieldType`): **String** 32 bytes (up to 31 characters, NUL terminated, every byte after the NUL written as zero), **Enum** 4 bytes (an i32; the labels are editor metadata and are not in the blob), **Vector2** 8 bytes, **Array** `4 + N * element` bytes: `u32 count` then `N` element slots, element = I32 4, Scalar 4, Guid 16 or Vector3 12, the slots past `count` written as zeros (so a cook is byte-reproducible; a reader clamps a damaged `count` to `N`). `World::loadPack` rejects a blob **longer** than the registered type's blob size, or one that is shorter and does not end on a field boundary (a cook/runtime mismatch). F5: component fields are only ever appended, so a blob that ends exactly on a field boundary before the last fields (a pack or scene cooked before a field existed) is accepted and the missing trailing fields keep their defaults (`readBlob`). Blobs start 4-byte aligned inside FDAT.

**Trimmed blobs (F6, `APPENDED_FIELDS()`).** A type may mark where fields were appended after it shipped (`Camera` from `orthographic`, `AudioSource` from `playOnAwake`, `SphereCollider.center`, `UIButton` from `navigation`). The cooker writes `COMP.fieldSize` without the trailing appended fields that still hold their default, down to the pre-append size at the least (`trimmedBlobSize`). The blob still ends on a field boundary, so `readBlob` accepts it and the loader fills the rest from the defaults. Result: a scene that never touches an appended field cooks to the same bytes as before the field existed. Types declared new in F6 (`Rigidbody`, `ParticleSystem`, ...) are never trimmed.

### Scheduler limits and UnityEvent (F3.05)

`CORO` (`PackSchedulerLimits`, 16 B, one record): `u32 maxCoroutines`, `u32 maxInvokes`, 2 reserved. They come from `ProjectSettings/ConsoleTarget.json` `budgets.maxCoroutines` and `budgets.maxInvokes` (both optional, default 64, 1 to 4096). The loader sizes the World's coroutine and Invoke pools from it (grow or shrink, only while nothing is running). A pack without `CORO` keeps the World's defaults (64 / 64), so old packs load unchanged. The cooker also writes `kMaxCoroutines` / `kMaxInvokes` into `console_target.gen.h`.

FDAT field type 7, `UnityEvent` (180 B): `u32 count` (0 to 4) then four listeners of 44 B: `Guid target` (object GUID, null = empty), `u32 typeHash` (FNV-1a 32 of the component type name), `u32 methodHash` (FNV-1a 32 of the `METHOD(Name)` name), `u8 argKind` (0 none, 1 Scalar, 2 i32, 3 bool, 4 object GUID), 3 pad, `u8 arg[16]` (Scalar or i32 in the first 4 bytes, bool in the first byte, a GUID in all 16). Targets are found by GUID when the event fires, so the same bytes work on every platform. The cooker rejects a listener whose target object, component type or `METHOD` does not exist. Types without a UnityEvent field cook exactly as before.

### Cooker output order

STRS, OBJS, XFRM, COMP, FDAT, MESH, VERT, MSHX (only with compact meshes), INPT, CTGT, CTG2, CORO, SCNE, BLOB, TEXR, AUDI, TMAP, ANIM, FONT, PH2D (only when `ProjectSettings/Physics2D.json` exists). Objects keep scene-file order; a parent may follow its child (the loader creates all objects first, then links parents).
| `SCNE` | `PackSceneRec` 32 B | F2.05 scene list in build-settings order: `Guid`, `nameStr` (file name without extension), `firstObject`, `objectCount`, reserved. OBJS, XFRM and COMP hold the objects of every scene, one contiguous block per scene; `objectIndex` and `parentIndex` stay global. A pack without SCNE is one scene. |
| `PH2D` | `PackPhysics2D` 128 B | F2.05 2D layer collision matrix, one record, only when `ProjectSettings/Physics2D.json` exists (see "Layer collision matrix"). |

## F6 content sections

All optional and absent from every pack cooked before the agent that owns them lands (Stage 1 to 3 of F6): `PCLP`, `ANIC` by B1, `DATA` by D3, `RSRC` by A3, `PHY3` by A2. `PackView::content()` returns `PackContent` with typed pointers and counts (empty without the sections); `PackView::open` checks every range (tables inside `BLOB`, transition and condition ranges, controller state indices, `RSRC` sorted) and refuses a bad pack with a log line naming the record. Payloads live in `BLOB`, 16-byte aligned, records hold byte offsets from the start of `BLOB`. Clip time is an integer count of ticks at the clip's own integer `fps` (deterministic on every profile). `Scalar` fields are `float` today.

| FourCC | Record | Contents |
|---|---|---|
| `PCLP` | `PackPropClip` 48 B | Property clips. `Guid`, `nameStr`, `fps` (> 0), `lengthTicks`, `flags` (bit 0 loop), `curveCount`, `curvesOffset`, `eventCount`, `eventsOffset`. |
| | `PackPropCurve` 32 B (BLOB) | One animated channel: `pathStr` (STRS offset of a child path such as `Arm/Hand`, empty = the Animator's object), `componentType` (type name hash), `fieldHash` (FNV-1a 32 of the field name), `i8 part` (-1 whole field, 0..3 = x y z w or r g b a), `u8 interp` (0 linear, 1 step), `u8 valueType` (`FieldType`), pad, `keyCount`, `keysOffset`, 2 reserved. |
| | `PackPropKey` 16 B (BLOB) | `u32 tick`, `Scalar value`, `Scalar inTangent`, `Scalar outTangent`. Keys sorted by tick. Color32 channels are 0..255. |
| | `PackPropEvent` 32 B (BLOB) | `u32 tick`, `functionStr`, `i32 intParameter`, `Scalar floatParameter`, `stringStr`, 3 reserved. Sorted by tick. Dispatched through `AnimationEvents::dispatch`. |
| | B1 additions (F6.03, no layout change) | `PackPropCurve::interp` 2 = smooth (cubic Hermite from the keys' `inTangent` / `outTangent`, per tick). A `Guid` curve (`valueType` = `FieldType::Guid`) keeps its values in a `Guid[keyCount]` table at BLOB offset `reserved[0]`; key `k` holds guid `k` and `PackPropKey::value` is unused. `PackPropCurve::componentType` is also the hash of the pseudo components `Transform` (fields `localPosition`, `localRotation`, `localScale`) and `GameObject` (field `active`). `PackAnimTransition::durationTicks` is in fixed steps of the project's `Time.json` `fixedTimestep` (default 1/60). The runtime keeps clip time as an exact integer (`fps` units of 1/65536 tick per 1/tickHz of a step); see [animation.md](animation.md). |
| `ANIC` | `PackAnimController` 64 B | Animator controllers. `Guid`, `nameStr`, `defaultState`, then offset/count pairs into BLOB for params (`PackAnimParam` 16 B: `nameHash`, `nameStr`, `type` 0 float / 1 int / 2 bool / 3 trigger, `defaultBits`), states (`PackAnimState` 48 B: `nameHash`, `nameStr`, `kind` 0 property clip / 1 skeletal `CLP3` / 2 empty, `flags` bit 0 loop, `Guid clip`, `Scalar speed`, `firstTransition`, `transitionCount`, `speedParam`), transitions (`PackAnimTransition` 32 B: `toState`, `flags` bit 0 has exit time, `exitTime`, `durationTicks`, `firstCondition`, `conditionCount`, `fromState` (-1 = any state), reserved) and conditions (`PackAnimCondition` 16 B: `param`, `mode` 0 If / 1 IfNot / 2 Greater / 3 Less / 4 Equals / 5 NotEqual, `thresholdBits`, reserved); `anyFirst`/`anyCount` name the any-state transitions. A state's transitions are contiguous from `firstTransition`; a transition's conditions are contiguous from `firstCondition`. |
| `DATA` | `PackDataAsset` 32 B | Data assets (ScriptableObject equivalent): `Guid`, `typeHash` (a reflected type), `nameStr`, `blobOffset`, `blobSize`. The blob is the type's reflected field blob (`writeBlob`), so appended fields stay readable. |
| `RSRC` | `PackResource` 32 B | `Resources.Load` names: `Guid`, `nameHash` (FNV-1a 32 of the lower-case path under a `Resources` folder, no extension), `nameStr`, `kind` (`PackResourceKind`: texture, audio, material, mesh, font, data asset, prefab, animator controller, property clip, scene), reserved. Sorted by `nameHash` (ties in cook order), so a lookup bisects (`PackContent::findResource`). |
| `PHY3` | `PackPhysics3D` 160 B | One record: `Scalar gravity[3]`, `flags` (bit 0 queries hit triggers), `masks[32]` (layer a collides with layer b when bit b of `masks[a]` is set, kept symmetric), `bounceThreshold`, `sleepThreshold`, 2 reserved. Absent = gravity (0, -9.81, 0), every layer collides. |

**`RSRC` as written by the cooker (F6.11).** Every asset below a folder named `Resources` (any depth under `Assets/`) gets a record; the name is the path under that folder without the extension (`Assets/Resources/Sfx/Boing.wav` is `Sfx/Boing`). The kind comes from the file name: `.png` texture, `.wav` audio, `.font.json` font, `.mat.json` material, `.gltf`/`.glb` mesh, `.prefab` prefab, `.controller.json` animator controller, `.clip.json` property clip, `.asset.json` data asset. Two assets of the same kind whose names differ only by case are a cook error. The assets themselves are in the pack as always (the cooker packs every asset of the project); `RSRC` only maps the name to the GUID. A project with no `Resources` folder has no `RSRC` section and its pack bytes are unchanged. `packResourceNameHash` (pack_format.h) is the one name hash: FNV-1a 32 over the lower-cased path with `\\` read as `/`.

**`PackSceneRec::reserved` bit 0 (`kSceneAdditive`, F6.11).** The cooker sets it for the scenes listed in `ProjectSettings/Project.json` under `"additiveScenes": [<scene guid>, ...]`. `World::loadPack` then sizes each component pool for the largest ordinary scene plus every flagged scene, so a level and its HUD or loading overlay fit together. Without the bit pools are sized for the largest single scene, exactly as before; an additive load that does not fit is refused with the pool's name (`SceneManager` logs it, `LoadSceneAsync` reports an operation that finishes without activating).

**`PCAP` (F6.13).** `PackPoolReserve` records `{typeHash, extra}`: extra component slots for run-time `Instantiate` and pools. Every pool is sized from the scene data plus a spare of 8; a game that clones more lists the type under `"componentReserve": {"MeshRenderer": 96}` in `ProjectSettings/Project.json` and `World::loadPack` adds `extra` to that type's pool (the sum must stay within `maxComponentsPerType`). No key, no section, pools sized as before. An unknown type name or a count outside 1 to 4096 is a cook error.

Prefabs need no runtime section: the cooker merges prefab instances and their overrides into ordinary objects at cook time.

## Lit and skinned meshes (F5, shared 3D)

Version stays 2. Every section below is optional; a pack without them reads exactly as before, and `MESH`, `VERT`, `MSHX` and `CTGT` are byte-identical. Structs are in `pack_format.h` with `static_assert`s on their sizes.

| FourCC | Record | Contents |
|---|---|---|
| `MSHL` | `PackMeshLit` 64 B, one per `MESH` record (same index) | `vertexFormat` (0 Baked = draw MESH/VERT as before; 2 LitF, 3 LitC16, 4 SkinF, 5 SkinC16), `vertexByteOffset` (into `VTX3`, multiple of 16), `vertexCount`, `material` (MATL index or `kInvalidIndex`), `skin` (SKIN index or `kInvalidIndex`), `firstBatch`, `batchCount` (into `BTCH`), `flags` (`kMeshWindingCCW` = 1, `kMeshNeedsNearClip` = 2), `boundsCenter[3]`, `boundsRadius` (mesh space), `decodeOffset[3]`, `decodeScale` (C16 only: position = offset + i16 x scale; scale is a power of two, one value for all axes). For a non-baked mesh `MESH.vertexCount` is **0**, so a renderer that does not know `MSHL` draws nothing. |
| `VTX3` | raw bytes | Vertices of the non-baked meshes, triangle lists, each mesh starting on a 16-byte boundary. |
| `BTCH` | `PackBoneBatch` 32 B | A skinned mesh is cut into runs of triangles that use at most 8 joints (GU weight *i* uses bone matrix *i*): `firstVertex`, `vertexCount` (mesh-relative, multiples of 3), `bones[8]` (SKIN joint index loaded into bone matrix *i*, `0xFF` = unused), `boneCount`, 3 reserved. |
| `MATL` | `PackMaterial` 32 B | `Guid`, `texture` (TEXR index or `kInvalidIndex`), `colorABGR` (multiplies vertex color and texture), `flags` (`kMatUnlit` = 1: skip lighting; `kMatAlphaTest` = 2: discard alpha below 128), reserved. |
| `SKIN` | `PackSkin` 32 B | `Guid`, `jointCount`, `jointsOffset` (BLOB, 16-byte aligned), 2 reserved. Joints are `PackJoint` 112 B: `i32 parent` (-1 root; parents come before children), `nameStr`, bind-pose local `t[3]`, `r[4]` (x y z w), `s[3]`, `invBind[16]` (inverse of the model-space bind matrix, column-major like `Matrix4`). |
| `CLP3` | `PackClip3` 48 B | `Guid`, `nameStr`, `skin` (SKIN index), `frameCount`, `fps` (integer), `loop`, `dataOffset` (BLOB), 2 reserved. Poses are `PackJointPose` 32 B (`r[4]`, `t[3]`, uniform `s`) x `jointCount` per frame, frame-major, resampled by the cooker to `fps`. |
| `CTG2` | `PackConsoleTargetExt` 32 B | `maxLightsPerObject`, `maxBonesPerDraw`, `maxSkinnedJoints`, `maxTextureSize`, `features` (bits `kTargetFeat*`: Lighting, Skinning, Fog, Osk, SaveDialog, Battery, CpuClock, TextureSwizzle), 3 reserved. 0 in a limit = not set (engine default: 4 lights on PSP, 8 elsewhere). Written by the cooker from `ConsoleTarget.json` `limits` and `features`. |

**Vertex structs** (fields in GU order: weights, texture, color, normal, position):

| Format | Struct | Bytes | GU vertex type |
|---|---|---|---|
| LitF | `PackVertexLit` `{float u, v; u32 color; float n[3]; float p[3]}` | 36 | `GU_TEXTURE_32BITF \| GU_COLOR_8888 \| GU_NORMAL_32BITF \| GU_VERTEX_32BITF` |
| SkinF | `PackVertexSkin` `{u8 w[8]; <PackVertexLit fields>}` | 44 | `GU_WEIGHT_8BIT` + the above, `GU_WEIGHTS(8)` |
| LitC16 | `PackVertexLitC16` `{u16 uv[2]; u32 color; i16 n[3]; i16 p[3]}` | 20 | `GU_TEXTURE_16BIT \| GU_COLOR_8888 \| GU_NORMAL_16BIT \| GU_VERTEX_16BIT` |
| SkinC16 | `PackVertexSkinC16` `{u8 w[8]; u16 uv[2]; u32 color; i16 n[3]; i16 p[3]}` | 28 | `GU_WEIGHT_8BIT` + the above |

Every size is a multiple of 4, the largest component alignment, so GU strides stay valid; there are no pad bytes. 16-bit UV = value / 32768 (range [0, 2), so a UV of 1.0 is representable), 16-bit normal = value / 32767, weight = value / 128 (the 8 weights of a vertex sum to 128), `color` is ABGR. `PackView::open` checks every range (VTX3 span, batch range, material, skin and joint indices, joint parents, clip pose range) and refuses a bad pack with a log line naming the mesh.

**Runtime tables.** `PackAssets` gains `materials`, `skins`, `clips3` (with `findMaterial/findSkin/findClip3`, `skinJoints(skin)`, `clipPoses(clip)`); `PackView::consoleTargetExt()` returns CTG2. `MeshTable` (renderer input) gains `lit`, `vtx3`, `vtx3Size`, `batches`, `batchCount`. `MeshRenderer::material` (a Guid appended to the component) names a MATL record; a mesh with no material stays baked exactly as before.

**Material asset (`*.mat.json`, `"format": 1`).** The cooker (agent A1) turns each into a MATL record:

```json
{ "format": 1, "texture": "<texture asset GUID or empty>", "color": [255, 255, 255, 255], "unlit": false, "alphaTest": false }
```

`texture` is an asset GUID (RULES 1.7), `color` is RGBA 0..255. Missing keys take the values shown. Until the material cook lands, the cooker accepts the new component types (`Light`, `RenderSettings`, `SkinnedMeshRenderer`, `Animation`) and the new `material` and `priority` fields like any other reflected data.

## Loading

1. `PackView::open(data, size)` validates magic, version, bounds and alignment, and exposes the sections.
2. `World::loadPack(view, &loaded, sceneIndex = 0)` sizes component pools (for the largest scene when SCNE lists several) from the COMP counts, creates objects, links parents, constructs components through `TypeRegistry` (by `typeHash`) and reads their FDAT blobs, then binds `MeshRenderer::meshIndex` from MESH. It creates only the objects of scene `sceneIndex`; `SceneManager::LoadScene` calls it again at the end of a frame after destroying the old objects. `LoadedPack` returns the mesh table, input actions, the target summary and `assets` (`PackAssets`, the v2 tables).

Cooking is deterministic: the same sources and ConsoleTarget produce a byte-identical pack. The cooker keeps `Library/Cooked/<target>/cook.manifest.json` (content hashes of every input plus the reflected field layout) and skips the cook when nothing changed.
