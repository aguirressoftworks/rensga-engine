// F6 contract (C0): the optional content sections PCLP, ANIC, DATA, RSRC, PHY3 (pack_format.h, pack-format.md). A pack without them
// is read exactly as before; with them PackView::content() gives typed tables and open() refuses a bad range.
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "rensga/core/components.h"
#include "rensga/core/pack_format.h"
#include "rensga/core/reflect.h"
#include "rensga_test.h"

using namespace rensga;

namespace {

// A minimal pack writer: header, section table, 16-byte aligned sections.
struct MiniPack {
  struct Sec { u32 fourcc; std::vector<u8> bytes; u32 count; };
  std::vector<Sec> secs;
  std::vector<u8> blob;
  std::vector<u8> strs{0};

  u32 str(const char* s) {
    u32 off = static_cast<u32>(strs.size());
    strs.insert(strs.end(), s, s + strlen(s) + 1);
    return off;
  }
  // Appends a payload to BLOB (16-byte aligned) and returns its offset.
  template <class T>
  u32 put(const std::vector<T>& v) {
    while (blob.size() % 16) blob.push_back(0);
    u32 off = static_cast<u32>(blob.size());
    const u8* p = reinterpret_cast<const u8*>(v.data());
    blob.insert(blob.end(), p, p + v.size() * sizeof(T));
    return off;
  }
  template <class T>
  void add(u32 fourcc, const std::vector<T>& v) {
    Sec s;
    s.fourcc = fourcc;
    const u8* p = reinterpret_cast<const u8*>(v.data());
    s.bytes.assign(p, p + v.size() * sizeof(T));
    s.count = static_cast<u32>(v.size());
    secs.push_back(s);
  }
  // Returns 16-byte aligned memory (leaked on purpose by the caller via free()).
  u8* build(size_t* size) {
    std::vector<Sec> all = secs;
    all.push_back({pack_section::STRS, strs, 1});
    all.push_back({pack_section::BLOB, blob.empty() ? std::vector<u8>(16, 0) : blob, 0});
    size_t off = sizeof(PackHeader) + all.size() * sizeof(PackSection);
    off = (off + 15) & ~size_t(15);
    std::vector<PackSection> table(all.size());
    for (size_t i = 0; i < all.size(); ++i) {
      table[i] = {all[i].fourcc, static_cast<u32>(off), static_cast<u32>(all[i].bytes.size()), all[i].count};
      off = (off + all[i].bytes.size() + 15) & ~size_t(15);
    }
    u8* mem = static_cast<u8*>(aligned_alloc(16, off));
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

Guid guidN(u32 n) {
  Guid g;
  g.bytes[0] = static_cast<u8>(n);
  g.bytes[15] = 1;
  return g;
}

// Fills every F6 section with a small valid set. Mutators let a test break one range.
struct Content {
  MiniPack mp;
  PackPropClip clip;
  PackAnimController ctl;
  std::vector<PackResource> res;
  void make(u32 resourceHashA = 10, u32 resourceHashB = 20) {
    // PCLP: one clip, one curve of two keys, one event
    std::vector<PackPropKey> keys(2);
    keys[0] = {0, 0.0f, 0, 0};
    keys[1] = {30, 5.0f, 0, 0};
    PackPropCurve curve = {};
    curve.componentType = fnv1a32("Light");
    curve.fieldHash = fnv1a32("intensity");
    curve.part = -1;
    curve.valueType = static_cast<u8>(FieldType::Scalar);
    curve.keyCount = 2;
    curve.keysOffset = mp.put(keys);
    std::vector<PackPropCurve> curves(1, curve);
    PackPropEvent ev = {};
    ev.tick = 15;
    ev.functionStr = mp.str("Footstep");
    ev.intParameter = 3;
    std::vector<PackPropEvent> events(1, ev);
    clip = {};
    clip.guid = guidN(1);
    clip.nameStr = mp.str("Pulse");
    clip.fps = 30;
    clip.lengthTicks = 30;
    clip.flags = kPropClipLoop;
    clip.curveCount = 1;
    clip.curvesOffset = mp.put(curves);
    clip.eventCount = 1;
    clip.eventsOffset = mp.put(events);
    mp.add(pack_section::PCLP, std::vector<PackPropClip>(1, clip));

    // ANIC: params {speed float, jump trigger}, states {Idle, Run}, Idle -> Run when speed > 0.5
    std::vector<PackAnimParam> params(2);
    params[0] = {fnv1a32("speed"), mp.str("speed"), static_cast<u32>(PackAnimParamType::Float), 0};
    params[1] = {fnv1a32("jump"), mp.str("jump"), static_cast<u32>(PackAnimParamType::Trigger), 0};
    std::vector<PackAnimState> states(2);
    states[0] = {fnv1a32("Idle"), mp.str("Idle"), static_cast<u32>(PackAnimStateKind::PropClip), kAnimStateLoop, guidN(1), 1.0f, 0, 1, kInvalidIndex};
    states[1] = {fnv1a32("Run"), mp.str("Run"), static_cast<u32>(PackAnimStateKind::PropClip), kAnimStateLoop, guidN(1), 1.0f, 1, 0, 0};
    std::vector<PackAnimTransition> trans(1);
    trans[0] = {1, 0, 0.0f, 6, 0, 1, 0, 0};
    std::vector<PackAnimCondition> conds(1);
    float half = 0.5f;
    u32 bits;
    memcpy(&bits, &half, 4);
    conds[0] = {0, static_cast<u32>(PackAnimCondMode::Greater), bits, 0};
    ctl = {};
    ctl.guid = guidN(2);
    ctl.nameStr = mp.str("Player");
    ctl.defaultState = 0;
    ctl.paramCount = 2;
    ctl.paramsOffset = mp.put(params);
    ctl.stateCount = 2;
    ctl.statesOffset = mp.put(states);
    ctl.transitionCount = 1;
    ctl.transitionsOffset = mp.put(trans);
    ctl.conditionCount = 1;
    ctl.conditionsOffset = mp.put(conds);
    ctl.anyFirst = 1;
    ctl.anyCount = 0;
    mp.add(pack_section::ANIC, std::vector<PackAnimController>(1, ctl));

    // DATA: a blob of the Light type (reflected fields)
    const TypeInfo* light = TypeRegistry::instance().find("Light");
    std::vector<u8> lb(blobSize(*light));
    Light l;
    l.intensity = 2.5f;
    writeBlob(*light, &l, lb.data());
    PackDataAsset d = {};
    d.guid = guidN(3);
    d.typeHash = light->hash;
    d.nameStr = mp.str("Torch");
    d.blobOffset = mp.put(lb);
    d.blobSize = static_cast<u32>(lb.size());
    mp.add(pack_section::DATA, std::vector<PackDataAsset>(1, d));

    // RSRC: sorted by name hash
    res.assign(2, PackResource());
    res[0] = {guidN(4), resourceHashA, mp.str("sfx/jump"), static_cast<u32>(PackResourceKind::Audio), 0};
    res[1] = {guidN(5), resourceHashB, mp.str("data/torch"), static_cast<u32>(PackResourceKind::DataAsset), 0};
    mp.add(pack_section::RSRC, res);

    PackPhysics3D ph = {};
    ph.gravity[1] = -9.81f;
    ph.flags = kPhy3QueriesHitTriggers;
    for (u32 i = 0; i < 32; ++i) ph.masks[i] = 0xFFFFFFFFu;
    ph.masks[3] &= ~(1u << 5);
    ph.masks[5] &= ~(1u << 3);
    mp.add(pack_section::PHY3, std::vector<PackPhysics3D>(1, ph));
  }
};

}  // namespace

TEST(pack_content_sections_read_back) {
  registerBuiltinComponents();
  Content c;
  c.make();
  size_t size;
  u8* mem = c.mp.build(&size);
  PackView v;
  CHECK(v.open(mem, size, "content"));
  const PackContent pc = v.content();
  CHECK_EQ(pc.propClipCount, 1u);
  CHECK_EQ(pc.controllerCount, 1u);
  CHECK_EQ(pc.dataCount, 1u);
  CHECK_EQ(pc.resourceCount, 2u);
  CHECK(pc.physics3d != nullptr);
  if (pc.propClipCount == 1 && pc.controllerCount == 1 && pc.dataCount == 1 && pc.physics3d) {
    const PackPropClip* clip = pc.findPropClip(guidN(1));
    CHECK(clip != nullptr);
    if (clip) {
      CHECK_EQ(clip->fps, 30u);
      const PackPropCurve* cv = pc.curves(*clip);
      CHECK_EQ(cv[0].fieldHash, fnv1a32("intensity"));
      CHECK_EQ(cv[0].keyCount, 2u);
      CHECK_NEAR(pc.keys(cv[0])[1].value, 5.0f, 1e-6f);
      CHECK_EQ(pc.keys(cv[0])[1].tick, 30u);
      CHECK_EQ(pc.events(*clip)[0].intParameter, 3);
      CHECK(strcmp(v.string(pc.events(*clip)[0].functionStr), "Footstep") == 0);
    }
    const PackAnimController* ac = pc.findController(guidN(2));
    CHECK(ac != nullptr);
    if (ac) {
      CHECK_EQ(pc.params(*ac)[1].type, static_cast<u32>(PackAnimParamType::Trigger));
      CHECK_EQ(pc.states(*ac)[0].nameHash, fnv1a32("Idle"));
      CHECK_EQ(pc.states(*ac)[0].transitionCount, 1u);
      CHECK_EQ(pc.transitions(*ac)[0].toState, 1);
      CHECK_EQ(pc.conditions(*ac)[0].mode, static_cast<u32>(PackAnimCondMode::Greater));
    }
    const PackDataAsset* da = pc.findData(guidN(3));
    CHECK(da != nullptr);
    if (da) {
      Light out;
      const TypeInfo* light = TypeRegistry::instance().findByHash(da->typeHash);
      CHECK(light != nullptr);
      CHECK(light && readBlob(*light, &out, pc.dataBlob(*da), da->blobSize));
      CHECK_EQ(out.intensity, 2.5f);
    }
    const PackResource* r = pc.findResource(20);
    CHECK(r != nullptr && r->guid == guidN(5));
    CHECK(pc.findResource(15) == nullptr);
    CHECK_EQ(pc.physics3d->flags, kPhy3QueriesHitTriggers);
    CHECK_EQ(pc.physics3d->masks[3] & (1u << 5), 0u);
  }
  free(mem);
}

TEST(pack_content_absent_sections_are_empty) {
  MiniPack mp;
  size_t size;
  u8* mem = mp.build(&size);
  PackView v;
  CHECK(v.open(mem, size, "empty"));
  const PackContent pc = v.content();
  CHECK_EQ(pc.propClipCount + pc.controllerCount + pc.dataCount + pc.resourceCount, 0u);
  CHECK(pc.physics3d == nullptr && pc.findResource(1) == nullptr && pc.findPropClip(guidN(1)) == nullptr);
  free(mem);
}

TEST(pack_content_bad_ranges_are_refused) {
  registerBuiltinComponents();
  struct Case { const char* what; void (*mutate)(Content&); };
  const Case cases[] = {
      {"clip curves past BLOB", [](Content& c) { c.clip.curvesOffset = 1u << 20; }},
      {"clip fps zero", [](Content& c) { c.clip.fps = 0; }},
      {"controller default state", [](Content& c) { c.ctl.defaultState = 9; }},
      {"controller states past BLOB", [](Content& c) { c.ctl.statesOffset = 1u << 20; }},
      {"any-state range", [](Content& c) { c.ctl.anyFirst = 5; }},
  };
  for (const Case& k : cases) {
    Content c;
    c.make();
    k.mutate(c);
    // Re-add the mutated records (make() added the good ones): replace the sections.
    for (auto& s : c.mp.secs) {
      if (s.fourcc == pack_section::PCLP) memcpy(s.bytes.data(), &c.clip, sizeof c.clip);
      if (s.fourcc == pack_section::ANIC) memcpy(s.bytes.data(), &c.ctl, sizeof c.ctl);
    }
    size_t size;
    u8* mem = c.mp.build(&size);
    PackView v;
    if (v.open(mem, size, "bad")) {
      CHECK(!"a bad content range was accepted");
      fprintf(stderr, "  case: %s\n", k.what);
    }
    free(mem);
  }
  // RSRC out of order
  Content c;
  c.make(50, 20);
  size_t size;
  u8* mem = c.mp.build(&size);
  PackView v;
  CHECK(!v.open(mem, size, "unsorted"));
  free(mem);
  // A record count larger than the section
  Content d;
  d.make();
  for (auto& s : d.mp.secs)
    if (s.fourcc == pack_section::DATA) s.count = 9;
  mem = d.mp.build(&size);
  CHECK(!v.open(mem, size, "count"));
  free(mem);
}
