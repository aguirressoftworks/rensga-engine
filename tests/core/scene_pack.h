// F6 (A3) test helper: builds a small in-memory pack with several scenes (OBJS, XFRM, COMP, FDAT, SCNE, STRS, BLOB) and optional
// RSRC / DATA sections, so SceneManager, Resources and the save tests need no cooker. 16-byte aligned memory from build().
#pragma once
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "rensga/core/pack_format.h"
#include "rensga/core/reflect.h"

namespace scenepack {
using namespace rensga;

struct Obj {
  std::string name;
  int parent = -1;                 // index inside the scene, -1 = root
  const TypeInfo* component = nullptr;  // one component per object (null = none)
  i32 firstI32 = 0;                // value of the component's first field when it is an i32 field
  bool active = true;
};

struct Builder {
  struct Sec { u32 fourcc; std::vector<u8> bytes; u32 count; };
  std::vector<PackObject> objs;
  std::vector<PackTransform> xf;
  std::vector<PackComponent> comps;
  std::vector<u8> fdat;
  std::vector<PackSceneRec> scenes;
  std::vector<u8> strs{0};
  std::vector<u8> blob;
  std::vector<PackResource> res;
  std::vector<PackDataAsset> data;
  std::vector<PackPoolReserve> reserve;
  PackConsoleTarget ctgt = {};
  bool haveCtgt = false;

  u32 str(const char* s) {
    u32 off = static_cast<u32>(strs.size());
    strs.insert(strs.end(), s, s + strlen(s) + 1);
    return off;
  }
  u32 blobPut(const void* p, u32 n) {
    while (blob.size() % 16) blob.push_back(0);
    u32 off = static_cast<u32>(blob.size());
    blob.insert(blob.end(), static_cast<const u8*>(p), static_cast<const u8*>(p) + n);
    return off;
  }
  void addScene(const char* name, const std::vector<Obj>& list, bool additiveHint = false) {
    PackSceneRec sr = {};
    sr.nameStr = str(name);
    sr.firstObject = static_cast<u32>(objs.size());
    sr.objectCount = static_cast<u32>(list.size());
    sr.reserved = additiveHint ? kSceneAdditive : 0;
    for (size_t i = 0; i < list.size(); ++i) {
      PackObject o = {};
      o.nameStr = str(list[i].name.c_str());
      o.parentIndex = list[i].parent < 0 ? -1 : static_cast<i32>(sr.firstObject) + list[i].parent;
      o.active = list[i].active ? 1 : 0;
      objs.push_back(o);
      PackTransform t = {};
      t.rotation[3] = 1;
      t.scale[0] = t.scale[1] = t.scale[2] = 1;
      xf.push_back(t);
      if (list[i].component) {
        const TypeInfo& ti = *list[i].component;
        std::vector<u8> mem(ti.size + 64);
        ti.construct(mem.data());
        if (ti.fieldCount && ti.fields[0].type == FieldType::I32) *reinterpret_cast<i32*>(mem.data() + ti.fields[0].offset) = list[i].firstI32;
        std::vector<u8> b(blobSize(ti));
        writeBlob(ti, mem.data(), b.data());
        PackComponent pc = {};
        pc.typeHash = ti.hash;
        pc.objectIndex = static_cast<u32>(objs.size() - 1);
        pc.fieldOffset = static_cast<u32>(fdat.size());
        pc.fieldSize = static_cast<u32>(b.size());
        fdat.insert(fdat.end(), b.begin(), b.end());
        while (fdat.size() % 16) fdat.push_back(0);
        comps.push_back(pc);
        if (ti.destruct) ti.destruct(mem.data());
      }
    }
    scenes.push_back(sr);
  }
  void addResource(const char* path, PackResourceKind kind, const Guid& g) {
    PackResource r = {};
    r.guid = g;
    r.nameHash = packResourceNameHash(path);
    r.nameStr = str(path);
    r.kind = static_cast<u32>(kind);
    res.push_back(r);
  }
  template <class T>
  static Sec sec(u32 fourcc, const std::vector<T>& v) {
    Sec s;
    s.fourcc = fourcc;
    const u8* p = reinterpret_cast<const u8*>(v.data());
    s.bytes.assign(p, p + v.size() * sizeof(T));
    s.count = static_cast<u32>(v.size());
    return s;
  }
  // Returns 16-byte aligned memory; the caller keeps it for the test's lifetime (free() it).
  u8* build(size_t* size) {
    std::vector<Sec> all;
    all.push_back(sec(pack_section::OBJS, objs));
    all.push_back(sec(pack_section::XFRM, xf));
    all.push_back(sec(pack_section::COMP, comps));
    Sec f;
    f.fourcc = pack_section::FDAT;
    f.bytes = fdat.empty() ? std::vector<u8>(16, 0) : fdat;
    f.count = static_cast<u32>(comps.size());
    all.push_back(f);
    all.push_back(sec(pack_section::SCNE, scenes));
    if (haveCtgt) {
      std::vector<PackConsoleTarget> v(1, ctgt);
      all.push_back(sec(pack_section::CTGT, v));
    }
    if (!data.empty()) all.push_back(sec(pack_section::DATA, data));
    if (!reserve.empty()) all.push_back(sec(pack_section::PCAP, reserve));
    if (!res.empty()) {
      std::vector<PackResource> sorted = res;
      for (size_t i = 1; i < sorted.size(); ++i)  // insertion sort by hash, stable
        for (size_t j = i; j > 0 && sorted[j].nameHash < sorted[j - 1].nameHash; --j) { PackResource t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t; }
      all.push_back(sec(pack_section::RSRC, sorted));
    }
    Sec s;
    s.fourcc = pack_section::STRS;
    s.bytes = strs;
    s.count = 1;
    all.push_back(s);
    Sec b;
    b.fourcc = pack_section::BLOB;
    b.bytes = blob.empty() ? std::vector<u8>(16, 0) : blob;
    b.count = 0;
    all.push_back(b);
    size_t off = sizeof(PackHeader) + all.size() * sizeof(PackSection);
    off = (off + 15) & ~size_t(15);
    std::vector<PackSection> table(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
      table[i] = {all[i].fourcc, static_cast<u32>(off), static_cast<u32>(all[i].bytes.size()), all[i].count};
      off = (off + all[i].bytes.size() + 15) & ~size_t(15);
    }
    u8* mem = static_cast<u8*>(aligned_alloc(16, (off + 15) & ~size_t(15)));
    memset(mem, 0, off);
    PackHeader h = {};
    memcpy(h.magic, "RPAK", 4);
    h.version = kPackVersion;
    h.sectionCount = static_cast<u32>(all.size());
    h.alignment = 16;
    h.totalSize = static_cast<u32>(off);
    memcpy(mem, &h, sizeof h);
    memcpy(mem + sizeof h, table.data(), table.size() * sizeof(PackSection));
    for (size_t i = 0; i < all.size(); ++i) memcpy(mem + table[i].offset, all[i].bytes.data(), all[i].bytes.size());
    *size = off;
    return mem;
  }
};
}  // namespace scenepack
