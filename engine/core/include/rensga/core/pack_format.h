// MVP-21: pack format v1 (spec: docs/rensga-engine/pack-format.md).
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
constexpr u32 INPT = makeFourCC('I', 'N', 'P', 'T');  // PackInputAction[count]
constexpr u32 CTGT = makeFourCC('C', 'T', 'G', 'T');  // PackConsoleTarget (one record)
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

// INPT record: 16 bytes.
enum class PackInputKind : u32 { Button = 0, Axis2 = 1 };
enum class PackInputSource : u32 {
  // Axis2 sources
  Nub = 0,
  DPad = 1,
  // Button sources
  South = 16, East, West, North, L, R, Start, Select, Up, Down, Left, Right
};
struct PackInputAction {
  u32 nameStr;  // byte offset in STRS
  u32 kind;     // PackInputKind
  u32 source;   // PackInputSource
  u32 reserved;
};
static_assert(sizeof(PackInputAction) == 16, "PackInputAction is 16 bytes");

// CTGT record: 48 bytes, the ConsoleTarget summary the runtime sizes itself from.
struct PackConsoleTarget {
  u32 profile;     // fourcc: 'P','S','P',' ' for PSP
  u32 scalarType;  // 0 = float, 1 = fixed 20.12 (reserved)
  u32 width, height;
  u32 ramBytes, vramBytes;
  u32 trianglesPerFrame;
  u32 maxObjects;
  u32 maxComponentsPerType;
  u32 reserved[3];
};
static_assert(sizeof(PackConsoleTarget) == 48, "PackConsoleTarget is 48 bytes");

// Read-only view over a loaded pack. open() validates magic/version/bounds.
struct PackView {
  const u8* base = nullptr;
  u32 size = 0;
  const PackHeader* header = nullptr;
  const PackSection* sections = nullptr;

  // Returns false and logs ("pack: unsupported version N ...") on any problem.
  bool open(const u8* data, size_t byteSize);
  const PackSection* find(u32 fourcc) const;
  template <class T>
  const T* records(u32 fourcc, u32* count = nullptr) const {
    const PackSection* s = find(fourcc);
    if (count) *count = s ? s->count : 0;
    return s ? reinterpret_cast<const T*>(base + s->offset) : nullptr;
  }
  const char* string(u32 byteOffset) const;  // from STRS, "" if out of range
  const char* stringByIndex(u32 index) const;  // n-th string of STRS (0 is ""), "" if out of range
  const u8* section(u32 fourcc) const {
    const PackSection* s = find(fourcc);
    return s ? base + s->offset : nullptr;
  }
};

}  // namespace rensga
