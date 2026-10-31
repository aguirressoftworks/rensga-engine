// MVP-21 / F2.03: pack format v2 (v1 still readable) (spec: docs/rensga-engine/pack-format.md).
// All values little-endian. Every section starts on a 16-byte boundary. The loader maps sections
// with no parsing and refuses another version with a log line.
#pragma once
#include "rensga/core/guid.h"
#include "rensga/core/scalar.h"
#include "rensga/core/types.h"

namespace rensga {

constexpr u32 makeFourCC(char a, char b, char c, char d) {
  return static_cast<u32>(static_cast<u8>(a)) | (static_cast<u32>(static_cast<u8>(b)) << 8) |
         (static_cast<u32>(static_cast<u8>(c)) << 16) | (static_cast<u32>(static_cast<u8>(d)) << 24);
}

constexpr u32 kPackVersion = 2;     // written by the cooker
constexpr u32 kPackMinVersion = 1;  // oldest version the runtime still reads (v1 OBJS has no tag)
constexpr u32 kPackAlignment = 16;
constexpr u32 kPackSectorAlignment = 2048;       // reserved for disc targets
constexpr u32 kPackFlagSectorAligned = 1u << 0;  // reserved: sections aligned to 2048

namespace pack_section {
constexpr u32 STRS = makeFourCC('S', 'T', 'R', 'S');  // string table: NUL-terminated strings; refs are byte offsets
constexpr u32 OBJS = makeFourCC('O', 'B', 'J', 'S');  // PackObject[count]
constexpr u32 XFRM = makeFourCC('X', 'F', 'R', 'M');  // PackTransform[count], parallel to OBJS (local space)
constexpr u32 COMP = makeFourCC('C', 'O', 'M', 'P');  // PackComponent[count]
constexpr u32 FDAT = makeFourCC('F', 'D', 'A', 'T');  // reflected field blobs
constexpr u32 MESH = makeFourCC('M', 'E', 'S', 'H');  // PackMeshRec[count]
constexpr u32 VERT = makeFourCC('V', 'E', 'R', 'T');  // PackVertex[count]
constexpr u32 MSHX = makeFourCC('M', 'S', 'H', 'X');  // F4: optional PackMeshExt (one record); absent = meshes are float PackVertex
constexpr u32 INPT = makeFourCC('I', 'N', 'P', 'T');  // PackInputAction[count]
constexpr u32 INPX = makeFourCC('I', 'N', 'P', 'X');  // F3.06: PackInputActionExt[count], parallel to INPT (absent in older packs)
constexpr u32 INPS = makeFourCC('I', 'N', 'P', 'S');  // F3.06: PackInputSettings (one record; absent = defaults)
constexpr u32 CTGT = makeFourCC('C', 'T', 'G', 'T');  // PackConsoleTarget (one record)
constexpr u32 CORO = makeFourCC('C', 'O', 'R', 'O');  // F3.05: PackSchedulerLimits (one record; absent = defaults 64 / 64)
// v2 asset sections. Records are POD and read in place; payload offsets point into BLOB.
constexpr u32 BLOB = makeFourCC('B', 'L', 'O', 'B');  // payload bytes: pixels, palettes, PCM, tiles, frame rects
constexpr u32 TEXR = makeFourCC('T', 'E', 'X', 'R');  // PackTexRec[count]
constexpr u32 AUDI = makeFourCC('A', 'U', 'D', 'I');  // PackAudioRec[count]
constexpr u32 TMAP = makeFourCC('T', 'M', 'A', 'P');  // PackTilemapRec[count]
constexpr u32 ANIM = makeFourCC('A', 'N', 'I', 'M');  // PackAnimRec[count] (one sprite clip each)
constexpr u32 FONT = makeFourCC('F', 'O', 'N', 'T');  // PackFontRec[count]
// F2.05: scene list. OBJS/XFRM/COMP hold the objects of every scene, one contiguous block per scene.
constexpr u32 SCNE = makeFourCC('S', 'C', 'N', 'E');  // PackSceneRec[count], in build-settings order
// F2.05: 2D layer collision matrix (ProjectSettings/Physics2D.json). Absent = every layer collides with every layer.
constexpr u32 PH2D = makeFourCC('P', 'H', '2', 'D');  // PackPhysics2D (one record)
// F5 shared 3D (all optional; a pack without them is read exactly as before). MESH, VERT, MSHX and CTGT are unchanged.
constexpr u32 MSHL = makeFourCC('M', 'S', 'H', 'L');  // PackMeshLit[MESH.count], parallel to MESH
constexpr u32 VTX3 = makeFourCC('V', 'T', 'X', '3');  // raw vertex bytes of lit / skinned meshes (each mesh 16-byte aligned)
constexpr u32 BTCH = makeFourCC('B', 'T', 'C', 'H');  // PackBoneBatch[count]
constexpr u32 MATL = makeFourCC('M', 'A', 'T', 'L');  // PackMaterial[count]
constexpr u32 SKIN = makeFourCC('S', 'K', 'I', 'N');  // PackSkin[count]; joints live in BLOB
constexpr u32 CLP3 = makeFourCC('C', 'L', 'P', '3');  // PackClip3[count]; poses live in BLOB
constexpr u32 CTG2 = makeFourCC('C', 'T', 'G', '2');  // PackConsoleTargetExt (one record)
}  // namespace pack_section

struct PackHeader {
  char magic[4];  // "RPAK"
  u32 version;    // kPackVersion
  u32 sectionCount;
  u32 alignment;  // 16 (or 2048 with kPackFlagSectorAligned)
  u32 totalSize;
  u32 flags;
  u32 reserved[2];  // pads the header to 32 bytes; the section table follows at offset 32
};
static_assert(sizeof(PackHeader) == 32, "PackHeader is 32 bytes");

struct PackSection {
  u32 fourcc;
  u32 offset;  // from the start of the pack, multiple of 16
  u32 size;    // bytes
  u32 count;   // number of records (STRS: number of strings)
};
static_assert(sizeof(PackSection) == 16, "PackSection is 16 bytes");

// OBJS record: 32 bytes.
struct PackObject {
  Guid guid;
  u32 nameStr;  // byte offset in STRS
  i32 parentIndex;  // index into OBJS, -1 = root
  u16 active;
  u16 layer;
  u16 tag;   // v2: index into STRS strings (STRS string 0 is "", so 0 = "Untagged"); v1 packs hold 0 here
  u16 pad;
};
static_assert(sizeof(PackObject) == 32, "PackObject is 32 bytes");

// XFRM record: 48 bytes (Scalar per profile; float for the PSP profile).
struct PackTransform {
  Scalar position[3];
  Scalar pad0;
  Scalar rotation[4];  // x y z w
  Scalar scale[3];
  Scalar pad1;
};
static_assert(sizeof(PackTransform) == 48, "PackTransform is 48 bytes");

// COMP record: 16 bytes. The field blob is FDAT[fieldOffset .. fieldOffset+fieldSize).
// Blob layout: the type's reflected fields in declaration order, raw little-endian values with no
// padding. Sizes: Scalar 4, I32 4, Bool 1, Vector3 12, Quaternion 16, Color32 4 (r,g,b,a), Guid 16.
struct PackComponent {
  u32 typeHash;  // FNV-1a 32 of the type name
  u32 objectIndex;
  u32 fieldOffset;  // relative to the FDAT section start
  u32 fieldSize;
};
static_assert(sizeof(PackComponent) == 16, "PackComponent is 16 bytes");

// MESH record: 16 bytes. Each MeshRenderer owns a baked mesh variant (vertex colors carry the
// shading), so a mesh belongs to one object. vertexOffset is a vertex INDEX into VERT.
struct PackMeshRec {
  u32 vertexOffset;
  u32 vertexCount;
  u32 primitiveType;  // 0 = triangle list
  u32 objectIndex;    // owning OBJS index whose MeshRenderer draws this mesh (kInvalidIndex = none)
};
static_assert(sizeof(PackMeshRec) == 16, "PackMeshRec is 16 bytes");

// GU vertex: GU_COLOR_8888 | GU_VERTEX_32BITF (color first, then position). 16 bytes.
struct PackVertex {
  u32 color;  // ABGR packed
  float x, y, z;
};
static_assert(sizeof(PackVertex) == 16, "PackVertex is 16 bytes");

// MSHX record: 32 bytes (F4, optional). Present only in a pack cooked for the psp profile itself (never for
// linux, windows or android). When absent, VERT holds PackVertex (float) as before. Format 1 is the compact PSP
// vertex, 12 bytes: GU_COLOR_8888 | GU_VERTEX_16BIT, `PackVertexCompact` below. The VERT section then holds
// PackVertexCompact[count] (each mesh's vertexOffset is a vertex INDEX of that type). The decoded position is
// offset[i] + xyz[i] * scale[i]. The VERT section's `count` is the number of vertices of the active format (the cooker
// writes size / sizeof(vertex type)). Each scale is a power of two (so the GU model matrix absorbs it exactly).
struct PackMeshExt {
  u32 format;       // 0 = float (same as no MSHX), 1 = compact 16-bit positions
  float offset[3];
  float scale[3];   // world units per i16 step, a power of two
  u32 reserved;
};
static_assert(sizeof(PackMeshExt) == 32, "PackMeshExt is 32 bytes");
constexpr u32 kMeshFormatFloat = 0, kMeshFormatCompact16 = 1;

struct PackVertexCompact {
  u32 color;  // ABGR packed
  i16 x, y, z;
  u16 pad;
};
static_assert(sizeof(PackVertexCompact) == 12, "PackVertexCompact is 12 bytes");

// INPT record: 16 bytes. Kinds: Button (held/pressed/released), Axis2 (Vector2, y up), Axis (one Scalar, -1..1).
enum class PackInputKind : u32 { Button = 0, Axis2 = 1, Axis = 2 };
enum class PackInputSource : u32 {
  // Axis2 sources (Composite: built from the four direction bindings in INPX)
  Nub = 0,
  DPad = 1,
  RightStick = 2,
  Composite = 3,
  // Button sources
  South = 16, East, West, North, L, R, Start, Select, Up, Down, Left, Right, L2, R2,
  // Axis (one value) sources; Composite makes an Axis from positive/negative bindings only
  AxisLeftX = 32, AxisLeftY, AxisRightX, AxisRightY, AxisL2, AxisR2,
};
// A binding in INPX is a button PackInputSource, a key (kInputKeyBase + KeyCode) or kInputNone.
constexpr u32 kInputKeyBase = 0x100;
constexpr u32 kInputNone = 0xFFFFFFFFu;
struct PackInputAction {
  u32 nameStr;  // byte offset in STRS
  u32 kind;     // PackInputKind
  u32 source;   // PackInputSource
  u32 reserved;
};
static_assert(sizeof(PackInputAction) == 16, "PackInputAction is 16 bytes");

// INPX record: 48 bytes, same index as INPT. bind[] by kind:
//   Button: bind[0] = second source.   Axis: bind[0..1] positive, bind[2..3] negative.
//   Axis2 Composite: bind[0..1] up, [2..3] down, [4..5] left, [6..7] right.
// gravity / sensitivity are Unity's keyboard ramp speeds (units per second); `dead` rescales a 1D analog axis.
struct PackInputActionExt {
  u32 bind[8];
  float gravity, sensitivity, dead;
  u32 flags;  // bit 0 snap (reverse direction passes through 0), bit 1 invert
};
static_assert(sizeof(PackInputActionExt) == 48, "PackInputActionExt is 48 bytes");
constexpr u32 kInputFlagSnap = 1, kInputFlagInvert = 2;

// INPS: radial dead zone and outer clamp of the sticks (InputActions.json "stickDeadZone" / "stickOuterClamp").
struct PackInputSettings {
  float deadZone, outerClamp;
  u32 reserved[2];
};
static_assert(sizeof(PackInputSettings) == 16, "PackInputSettings is 16 bytes");

constexpr u32 kTargetCapRumble = 1u << 0;  // the target's pad has vibration motors (ConsoleTarget "capabilities".rumble)

// CTGT record: 48 bytes, the ConsoleTarget summary the runtime sizes itself from.
struct PackConsoleTarget {
  u32 profile;     // fourcc: 'P','S','P',' ' for PSP
  u32 scalarType;  // 0 = float, 1 = fixed 20.12 (reserved)
  u32 width, height;
  u32 ramBytes, vramBytes;
  u32 trianglesPerFrame;
  u32 maxObjects;
  u32 maxComponentsPerType;
  u32 audioSampleRate;  // mixer rate (22050 or 44100); 0 in packs cooked before F2.04 (use 22050)
  u32 audioVoices;      // voice budget of the target; 0 = default 16
  u32 capabilities;     // F3.06: kTargetCap* bits (0 in packs cooked before)
};
static_assert(sizeof(PackConsoleTarget) == 48, "PackConsoleTarget is 48 bytes");

// CORO record: 16 bytes. Pool sizes of the coroutine and Invoke scheduler (ConsoleTarget budgets.maxCoroutines / maxInvokes).
struct PackSchedulerLimits {
  u32 maxCoroutines;
  u32 maxInvokes;
  u32 reserved[2];
};
static_assert(sizeof(PackSchedulerLimits) == 16, "PackSchedulerLimits is 16 bytes");

// ---- v2 asset records. Every record starts with the asset GUID (RULES 1.7); *Offset fields are byte
// offsets from the start of BLOB and every payload starts on a 16-byte boundary inside BLOB. ----

// F4 texture contract (frozen). Formats 0..2 are unchanged from v2.
//   RGBA5551 is GU_PSM_5551 (R bits 0-4, G 5-9, B 10-14, A bit 15), RGBA4444 is GU_PSM_4444 (R 0-3, G 4-7, B 8-11, A 12-15),
//   both little-endian u16 per pixel, rows tightly packed (storedWidth * 2 bytes).
enum class PackTexFormat : u32 { RGBA8 = 0, CLUT4 = 1, CLUT8 = 2, RGBA5551 = 3, RGBA4444 = 4 };
constexpr u32 kPackTexFormatMax = 4;  // validateAssets accepts format <= this

// PackTexRec::flags bits.
//   kTexSwizzled: the pixel bytes are stored in the PSP swizzle order: 8-row bands top to bottom; inside a band,
//   16-byte columns left to right, each column is 8 rows of 16 bytes (row-major). The row pitch of the unswizzled
//   image is storedWidth * bytesPerPixel (CLUT4: storedWidth / 2). Only the PSP renderer reads swizzled data;
//   the GL and GS renderers return 0 from uploadTexture when flags != 0.
constexpr u16 kTexSwizzled = 1u << 0;

// TEXR: RGBA8 pixels are width*height*4 bytes (R,G,B,A). CLUT8 is one index byte per pixel; CLUT4
// packs two indices per byte, low nibble first (rows padded to a whole byte). The palette is
// paletteCount RGBA8 u32 entries (little-endian R,G,B,A byte order, R in the low byte = GU_PSM_8888) at
// paletteOffset (16 or 256, 16-byte aligned).
//
// `width` / `height` are ALWAYS the source pixel size (what scripts and UVs address). When the cooker
// pads the image (PSP: power of two, minimum 32 px wide for CLUT4, 16 for CLUT8, 8 for 16/32-bit, minimum 8
// high, padding bytes zero) `storedWidth` x `storedHeight` is the size of the stored pixel array (pixelBytes
// covers the stored size). 0 means "same as width / height" (every v2 pack). Layout is byte-compatible with
// v2: the old u32 paletteCount (value 0, 16 or 256) reads the same as the new u16 paletteCount + u16 flags(0),
// and the old u32 reserved (0) as storedWidth(0) + storedHeight(0) on a little-endian target.
struct PackTexRec {
  Guid guid;
  u32 width, height;  // source pixel size
  u32 format;         // PackTexFormat
  u32 pixelOffset;
  u32 pixelBytes;
  u32 paletteOffset;  // 0 when format is RGBA8 / RGBA5551 / RGBA4444
  u16 paletteCount;   // 0, 16 or 256
  u16 flags;          // kTex* bits
  u16 storedWidth;    // 0 = width
  u16 storedHeight;   // 0 = height
  u32 storedW() const { return storedWidth ? storedWidth : width; }
  u32 storedH() const { return storedHeight ? storedHeight : height; }
};
static_assert(sizeof(PackTexRec) == 48, "PackTexRec is 48 bytes");

// AUDI: interleaved signed 16-bit PCM, frameCount frames of `channels` samples.
struct PackAudioRec {
  Guid guid;
  u32 sampleRate;
  u32 channels;    // 1 or 2
  u32 frameCount;  // frames per channel
  u32 loopStart;   // frames
  u32 loopEnd;     // frames, exclusive; equals frameCount when the whole clip loops
  u32 dataOffset;
  u32 loops;       // 0 = one-shot, 1 = loops between loopStart and loopEnd
  u32 reserved;
};
static_assert(sizeof(PackAudioRec) == 48, "PackAudioRec is 48 bytes");

// TMAP: tiles are u16 tileset indices, row-major, width*height entries at tilesOffset. Tile i of the
// tileset (row-major in the tileset texture) has the flag byte solid[i] (u8 array at solidOffset,
// solidCount entries; indices past solidCount have no flags). A byte of 1 is a full solid block, so packs
// written before the slope flags existed read the same way. Bits are in PackTileFlag below.
struct PackTilemapRec {
  Guid guid;
  u32 width, height;   // in tiles
  u32 tileSize;        // pixels, square
  u32 tilesetTexture;  // index into TEXR
  u32 tilesOffset;
  u32 solidOffset;
  u32 solidCount;
  u32 reserved;
};
static_assert(sizeof(PackTilemapRec) == 48, "PackTilemapRec is 48 bytes");

// Tile flag byte (one per tileset tile). Bit 0 alone = full solid block (the original meaning).
namespace PackTileFlag {
constexpr u8 kSolid = 1u << 0;    // blocks from every side
constexpr u8 kOneWay = 1u << 1;   // blocks only a box falling onto its top edge
constexpr u8 kSlopeShift = 2;     // bits 2..4: slope shape (PackSlope), 0 = none
constexpr u8 kSlopeMask = 7u << kSlopeShift;
}  // namespace PackTileFlag
// Slope shapes. The surface is a straight line across the tile; heights are in sixteenths of the tile height,
// measured up from the tile's bottom edge. L = the high side is on the left (\), R = high side on the right (/).
//   45L: 16 -> 0     45R: 0 -> 16
//   22L_hi: 16 -> 8  22L_lo: 8 -> 0     (a 22.5 degree slope spans two tiles, high tile on the left)
//   22R_lo: 0 -> 8   22R_hi: 8 -> 16    (high tile on the right)
enum PackSlope : u8 { kSlopeNone = 0, kSlope45L, kSlope45R, kSlope22L_lo, kSlope22L_hi, kSlope22R_lo, kSlope22R_hi };
inline void packSlopeHeights(u32 slope, i32* left, i32* right) {
  static const i8 h[8][2] = {{0, 0}, {16, 0}, {0, 16}, {8, 0}, {16, 8}, {0, 8}, {8, 16}, {0, 0}};
  *left = h[slope & 7][0];
  *right = h[slope & 7][1];
}

// PH2D: layer collision matrix. masks[a] bit b set = layer a collides with layer b (kept symmetric by the cooker).
struct PackPhysics2D {
  u32 masks[32];
};
static_assert(sizeof(PackPhysics2D) == 128, "PackPhysics2D is 128 bytes");

struct PackFrameRect {
  u16 x, y, w, h;  // pixels in the texture
};
static_assert(sizeof(PackFrameRect) == 8, "PackFrameRect is 8 bytes");

// ANIM: one sprite clip. Frames are PackFrameRect[frameCount] at framesOffset.
struct PackAnimRec {
  Guid guid;
  u32 nameStr;  // byte offset in STRS
  u32 texture;  // index into TEXR
  u32 frameCount;
  u32 framesOffset;
  u32 fps;      // frames per second (integer: deterministic on every profile)
  u32 loop;     // 0 or 1
  u32 reserved[2];
};
static_assert(sizeof(PackAnimRec) == 48, "PackAnimRec is 48 bytes");

constexpr u32 kPackFontMaxChars = 128;
// FONT: a bitmap font laid out as a grid of glyphW x glyphH cells, `columns` per row, starting at
// firstChar. Self-contained so IRenderer::drawText can take it by reference.
struct PackFontRec {
  Guid guid;
  u32 texture;  // index into TEXR
  u16 glyphW, glyphH;
  u16 firstChar, charCount;  // charCount <= kPackFontMaxChars
  u16 columns, lineHeight;
  u8 advance[kPackFontMaxChars];  // pixels to move after each glyph; index = char - firstChar
};
static_assert(sizeof(PackFontRec) == 16 + 4 + 12 + 128, "PackFontRec layout");

// SCNE record: scene `i` owns OBJS[firstObject, firstObject + objectCount). COMP objectIndex and MESH objectIndex
// stay global (index into OBJS); parentIndex too. Packs without SCNE hold a single scene = all objects.
struct PackSceneRec {
  Guid guid;
  u32 nameStr;  // byte offset in STRS (file name without extension)
  u32 firstObject;
  u32 objectCount;
  u32 reserved;
};
static_assert(sizeof(PackSceneRec) == 32, "PackSceneRec is 32 bytes");


// ---- F5 shared 3D records (spec: docs/rensga-engine/pack-format.md, "Lit and skinned meshes"). ----

// MSHL: one record per MESH entry, same index. vertexFormat 0 (Baked) means "draw MESH/VERT as before" and the other
// fields are ignored. For every other format MESH.vertexCount is 0 (an old renderer draws nothing) and the vertices
// are in VTX3 at vertexByteOffset. Meshes of a non-baked format are triangle lists.
enum class PackVertexFormat : u32 { Baked = 0, LitF = 2, LitC16 = 3, SkinF = 4, SkinC16 = 5 };  // 1 is reserved
constexpr u32 kMeshWindingCCW = 1u << 0;       // triangles are counter-clockwise (the default 0 = clockwise as the GU wants)
constexpr u32 kMeshNeedsNearClip = 1u << 1;    // big mesh: the PSP renderer clips against the near plane on the CPU
struct PackMeshLit {
  u32 vertexFormat;      // PackVertexFormat
  u32 vertexByteOffset;  // into VTX3, multiple of 16
  u32 vertexCount;       // vertices of the mesh (multiple of 3)
  u32 material;          // MATL index or kInvalidIndex
  u32 skin;              // SKIN index or kInvalidIndex (Skin* formats need one)
  u32 firstBatch;        // into BTCH (Skin* formats; 0 batches = the whole mesh is one batch with bones 0..7)
  u32 batchCount;
  u32 flags;             // kMesh*
  float boundsCenter[3];  // mesh space
  float boundsRadius;
  float decodeOffset[3];  // C16 only: position = decodeOffset + i16 * decodeScale (a power of two, uniform)
  float decodeScale;
  bool isBaked() const { return vertexFormat == 0; }
  bool isSkinned() const { return vertexFormat == 4 || vertexFormat == 5; }
  bool isC16() const { return vertexFormat == 3 || vertexFormat == 5; }
};
static_assert(sizeof(PackMeshLit) == 64, "PackMeshLit is 64 bytes");

// VTX3 vertex structs, fields in GU order (weights, texture, color, normal, position) so each is one GU vertex type:
//   Lit    GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF | GU_VERTEX_32BITF
//   Skin   GU_WEIGHT_8BIT | the same, with GU_WEIGHTS(8)
//   LitC16 GU_TEXTURE_16BIT | GU_COLOR_8888 | GU_NORMAL_16BIT | GU_VERTEX_16BIT
//   SkinC16 GU_WEIGHT_8BIT | the C16 fields
// Sizes: 36, 44, 20, 28 bytes. Each is a multiple of 4 (the largest component alignment), so GU strides stay valid.
// 16-bit texture coordinates are value / 32768 (u, v in [0, 2) so a UV of exactly 1 still fits); 16-bit normals are
// value / 32767; 8-bit weights are value / 128 and the 8 weights of a vertex sum to 128. u32 color is ABGR.
struct PackVertexLit {
  float u, v;
  u32 color;
  float n[3];
  float p[3];
};
static_assert(sizeof(PackVertexLit) == 36, "PackVertexLit is 36 bytes");
struct PackVertexSkin {
  u8 w[8];
  float u, v;
  u32 color;
  float n[3];
  float p[3];
};
static_assert(sizeof(PackVertexSkin) == 44, "PackVertexSkin is 44 bytes");
struct PackVertexLitC16 {
  u16 uv[2];
  u32 color;
  i16 n[3];
  i16 p[3];
};
static_assert(sizeof(PackVertexLitC16) == 20, "PackVertexLitC16 is 20 bytes");
struct PackVertexSkinC16 {
  u8 w[8];
  u16 uv[2];
  u32 color;
  i16 n[3];
  i16 p[3];
};
static_assert(sizeof(PackVertexSkinC16) == 28, "PackVertexSkinC16 is 28 bytes");
inline u32 packVertexStride(u32 format) {
  switch (format) {
    case 2: return sizeof(PackVertexLit);
    case 3: return sizeof(PackVertexLitC16);
    case 4: return sizeof(PackVertexSkin);
    case 5: return sizeof(PackVertexSkinC16);
    default: return 0;
  }
}

// BTCH: a skinned mesh is split into runs of triangles that use at most 8 joints (GU weight i uses bone matrix i).
// firstVertex / vertexCount are vertex indices RELATIVE TO THE MESH (multiples of 3 so triangles stay whole).
// bones[i] is the SKIN joint index loaded into GU bone matrix i; 0xFF = unused slot. Vertex weight slot i refers to bones[i].
constexpr u32 kMaxBatchBones = 8;
constexpr u8 kBatchBoneUnused = 0xFF;
struct PackBoneBatch {
  u32 firstVertex;
  u32 vertexCount;
  u8 bones[8];
  u32 boneCount;
  u32 reserved[3];
};
static_assert(sizeof(PackBoneBatch) == 32, "PackBoneBatch is 32 bytes");

// MATL: a Material (.mat.json). texture is a TEXR index (kInvalidIndex = untextured). colorABGR multiplies the vertex
// color and texture. kMatUnlit skips lighting (vertex color x texture x color only); kMatAlphaTest discards alpha < 128.
constexpr u32 kMatUnlit = 1u << 0, kMatAlphaTest = 1u << 1;
struct PackMaterial {
  Guid guid;
  u32 texture;
  u32 colorABGR;
  u32 flags;
  u32 reserved;
};
static_assert(sizeof(PackMaterial) == 32, "PackMaterial is 32 bytes");

// SKIN: jointCount PackJoint records at jointsOffset in BLOB, parents before children (parent -1 = root). The joint
// transform is the bind-pose LOCAL TRS; invBind is the inverse of the joint's bind-pose model-space matrix
// (column-major, the same layout as Matrix4).
struct PackSkin {
  Guid guid;
  u32 jointCount;
  u32 jointsOffset;
  u32 reserved[2];
};
static_assert(sizeof(PackSkin) == 32, "PackSkin is 32 bytes");
struct PackJoint {
  i32 parent;
  u32 nameStr;  // byte offset in STRS
  float t[3];
  float r[4];  // x y z w
  float s[3];
  float invBind[16];
};
static_assert(sizeof(PackJoint) == 112, "PackJoint is 112 bytes");

// CLP3: a skeletal clip resampled by the cooker to `fps` (integer) frames per second. dataOffset points at
// PackJointPose[frameCount * jointCount] in BLOB (frame-major). Time is an integer tick count on the fixed step.
struct PackClip3 {
  Guid guid;
  u32 nameStr;
  u32 skin;  // SKIN index
  u32 frameCount;
  u32 fps;
  u32 loop;  // 0 or 1
  u32 dataOffset;
  u32 reserved[2];
};
static_assert(sizeof(PackClip3) == 48, "PackClip3 is 48 bytes");
struct PackJointPose {
  float r[4];  // x y z w
  float t[3];
  float s;     // uniform scale
};
static_assert(sizeof(PackJointPose) == 32, "PackJointPose is 32 bytes");

// CTG2: ConsoleTarget extension (CTGT stays byte-identical). Zero fields mean "not set, use the engine default".
constexpr u32 kTargetFeatLighting = 1u << 0, kTargetFeatSkinning = 1u << 1, kTargetFeatFog = 1u << 2, kTargetFeatOsk = 1u << 3,
              kTargetFeatSaveDialog = 1u << 4, kTargetFeatBattery = 1u << 5, kTargetFeatCpuClock = 1u << 6,
              kTargetFeatTextureSwizzle = 1u << 7;
struct PackConsoleTargetExt {
  u32 maxLightsPerObject;  // PSP 4 (GU hardware lights), 0 = default
  u32 maxBonesPerDraw;     // PSP 8
  u32 maxSkinnedJoints;    // joints of one skeleton the palette pool holds
  u32 maxTextureSize;      // PSP 512
  u32 features;            // kTargetFeat* bits
  u32 reserved[3];
};
static_assert(sizeof(PackConsoleTargetExt) == 32, "PackConsoleTargetExt is 32 bytes");

// Typed pointers into a v2 pack's asset sections. Valid while the pack memory lives.
struct PackAssets {
  const u8* blob = nullptr;
  u32 blobSize = 0;
  const PackTexRec* textures = nullptr;
  u32 textureCount = 0;
  const PackAudioRec* audio = nullptr;
  u32 audioCount = 0;
  const PackTilemapRec* tilemaps = nullptr;
  u32 tilemapCount = 0;
  const PackAnimRec* anims = nullptr;
  u32 animCount = 0;
  const PackFontRec* fonts = nullptr;
  u32 fontCount = 0;
  // F5 shared 3D tables (empty without the sections).
  const PackMaterial* materials = nullptr;
  u32 materialCount = 0;
  const PackSkin* skins = nullptr;
  u32 skinCount = 0;
  const PackClip3* clips3 = nullptr;
  u32 clip3Count = 0;

  const PackTexRec* findTexture(const Guid& g) const {
    for (u32 i = 0; i < textureCount; ++i) if (textures[i].guid == g) return &textures[i];
    return nullptr;
  }
  const PackAudioRec* findAudio(const Guid& g) const {
    for (u32 i = 0; i < audioCount; ++i) if (audio[i].guid == g) return &audio[i];
    return nullptr;
  }
  const PackTilemapRec* findTilemap(const Guid& g) const {
    for (u32 i = 0; i < tilemapCount; ++i) if (tilemaps[i].guid == g) return &tilemaps[i];
    return nullptr;
  }
  const PackAnimRec* findAnim(const Guid& g) const {
    for (u32 i = 0; i < animCount; ++i) if (anims[i].guid == g) return &anims[i];
    return nullptr;
  }
  const PackFontRec* findFont(const Guid& g) const {
    for (u32 i = 0; i < fontCount; ++i) if (fonts[i].guid == g) return &fonts[i];
    return nullptr;
  }
  const PackClip3* findClip3(const Guid& g) const {
    for (u32 i = 0; i < clip3Count; ++i) if (clips3[i].guid == g) return &clips3[i];
    return nullptr;
  }
  const PackMaterial* findMaterial(const Guid& g) const {
    for (u32 i = 0; i < materialCount; ++i) if (materials[i].guid == g) return &materials[i];
    return nullptr;
  }
  const PackSkin* findSkin(const Guid& g) const {
    for (u32 i = 0; i < skinCount; ++i) if (skins[i].guid == g) return &skins[i];
    return nullptr;
  }
  const PackJoint* skinJoints(const PackSkin& k) const { return reinterpret_cast<const PackJoint*>(blob + k.jointsOffset); }
  const PackJointPose* clipPoses(const PackClip3& c) const { return reinterpret_cast<const PackJointPose*>(blob + c.dataOffset); }
  // Payload accessors (offsets are into BLOB).
  const u8* texturePixels(const PackTexRec& t) const { return blob + t.pixelOffset; }
  const u32* texturePalette(const PackTexRec& t) const {
    return t.paletteCount ? reinterpret_cast<const u32*>(blob + t.paletteOffset) : nullptr;
  }
  const i16* audioSamples(const PackAudioRec& a) const { return reinterpret_cast<const i16*>(blob + a.dataOffset); }
  const u16* tilemapTiles(const PackTilemapRec& m) const { return reinterpret_cast<const u16*>(blob + m.tilesOffset); }
  const u8* tilemapSolid(const PackTilemapRec& m) const { return blob + m.solidOffset; }
  const PackFrameRect* animFrames(const PackAnimRec& a) const {
    return reinterpret_cast<const PackFrameRect*>(blob + a.framesOffset);
  }
};

// Read-only view over a loaded pack. open() validates magic/version/bounds.
struct PackView {
  const u8* base = nullptr;
  u32 size = 0;
  const PackHeader* header = nullptr;
  const PackSection* sections = nullptr;

  // Returns false and logs ("pack: unsupported version N ...") on any problem.
  // `name` (the pack's file name) is added to every error line so a bad pack can be found.
  bool open(const u8* data, size_t byteSize, const char* name = nullptr);
  const PackSection* find(u32 fourcc) const;
  template <class T>
  const T* records(u32 fourcc, u32* count = nullptr) const {
    const PackSection* s = find(fourcc);
    if (count) *count = s ? s->count : 0;
    return s ? reinterpret_cast<const T*>(base + s->offset) : nullptr;
  }
  // F5: the CTG2 record, nullptr when absent (packs cooked before F5).
  const PackConsoleTargetExt* consoleTargetExt() const { return records<PackConsoleTargetExt>(pack_section::CTG2); }
  const char* string(u32 byteOffset) const;  // from STRS, "" if out of range
  const char* stringByIndex(u32 index) const;  // n-th string of STRS (0 is ""), "" if out of range
  // v2 asset tables (all empty for a v1 pack or a pack without assets).
  PackAssets assets() const;
  bool validateAssets(const char* name) const;
  const u8* section(u32 fourcc) const {
    const PackSection* s = find(fourcc);
    return s ? base + s->offset : nullptr;
  }
};

}  // namespace rensga
