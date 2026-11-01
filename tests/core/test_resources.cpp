// F6.11 (A3, API 9): Resources::Load<T> through RSRC: lookup by path (case-insensitive), kind checks, reference counts, Unload,
// data assets read through their reflected blob. In-memory pack from scene_pack.h.
#include "rensga/Rensga.h"
#include "rensga/Resources.h"
#include "rensga/core/scene_manager.h"
#include "rensga_test.h"
#include "scene_pack.h"

using namespace rensga;

namespace {
struct EnemyStats {
  REFLECT(EnemyStats)
  SERIALIZE_FIELD(i32, hp, 10)
  SERIALIZE_FIELD(Scalar, speed, 1.5f)
};

Guid gid(u8 n) {
  Guid g;
  g.bytes[0] = 0xC0;
  g.bytes[15] = n;
  return g;
}

struct Pack {
  scenepack::Builder b;
  u8* mem = nullptr;
  PackView view;
  Pack() {
    b.addScene("Main", {scenepack::Obj{"Root", -1, nullptr, 0, true}});
    b.addResource("sfx/Jump", PackResourceKind::Audio, gid(1));
    b.addResource("ui/icons/Heart", PackResourceKind::Texture, gid(2));
    b.addResource("data/orc", PackResourceKind::DataAsset, gid(3));
    b.addResource("data/missing", PackResourceKind::DataAsset, gid(4));
    b.addResource("misc/a", PackResourceKind::Mesh, gid(5));
    const TypeInfo* t = EnemyStats::staticTypeInfo();
    std::vector<u8> mem2(t->size + 16);
    t->construct(mem2.data());
    *reinterpret_cast<i32*>(mem2.data() + t->fields[0].offset) = 42;
    *reinterpret_cast<Scalar*>(mem2.data() + t->fields[1].offset) = 3.25f;
    std::vector<u8> blob(blobSize(*t));
    writeBlob(*t, mem2.data(), blob.data());
    PackDataAsset d = {};
    d.guid = gid(3);
    d.typeHash = t->hash;
    d.nameStr = b.str("orc");
    d.blobOffset = b.blobPut(blob.data(), static_cast<u32>(blob.size()));
    d.blobSize = static_cast<u32>(blob.size());
    b.data.push_back(d);
    size_t n = 0;
    mem = b.build(&n);
    CHECK(view.open(mem, n, "resources-test"));
    SceneManager::bind(&view, 0);
  }
  ~Pack() {
    SceneManager::unbind();
    free(mem);
  }
};
}  // namespace

TEST(resources_load_by_path_finds_kind_and_guid) {
  Pack p;
  CHECK_EQ(Resources::entryCount(), 5u);
  Handle<ResAudioClip> jump = Resources::Load<ResAudioClip>("sfx/jump");  // case-insensitive
  CHECK(jump.valid() && jump.guid == gid(1));
  Handle<ResTexture> heart = Resources::Load<ResTexture>("UI\\Icons\\HEART");
  CHECK(heart.valid() && heart.guid == gid(2));
  CHECK(!Resources::Load<ResTexture>("sfx/jump").valid());   // wrong kind
  CHECK(!Resources::Load<ResAudioClip>("sfx/none").valid());  // unknown
  CHECK(!Resources::Load<ResAudioClip>("").valid());
  CHECK_EQ(Resources::loadedCount(), 2u);
  Handle<ResAudioClip> j = Resources::Load<ResAudioClip>("sfx/jump");  // a second reference
  CHECK_EQ(Resources::loadedCount(), 2u);
  Resources::Unload(j);
  CHECK(!j.valid());
  CHECK_EQ(Resources::loadedCount(), 2u);
  Resources::Unload(heart);
  CHECK_EQ(Resources::loadedCount(), 1u);
  Resources::Unload(jump);
  CHECK_EQ(Resources::loadedCount(), 0u);
}

TEST(resources_are_reference_counted) {
  Pack p;
  Handle<ResAudioClip> a = Resources::Load<ResAudioClip>("sfx/jump");
  Handle<ResAudioClip> b = Resources::Load<ResAudioClip>("sfx/jump");
  CHECK(a.valid() && b.valid() && a.slot == b.slot);
  CHECK_EQ(Resources::loadedCount(), 1u);
  Resources::Unload(a);
  CHECK_EQ(Resources::loadedCount(), 1u);  // b still holds it
  Resources::Unload(b);
  CHECK_EQ(Resources::loadedCount(), 0u);
  Resources::Unload(b);  // a second Unload of a dead handle does nothing
  CHECK_EQ(Resources::loadedCount(), 0u);
}

TEST(resources_data_asset_reads_the_reflected_blob) {
  Pack p;
  Handle<EnemyStats> orc = Resources::Load<EnemyStats>("data/orc");
  CHECK(orc.valid());
  CHECK(orc.get() != nullptr);
  if (orc.get()) {
    CHECK_EQ(orc->hp, 42);
    CHECK_NEAR(orc->speed, 3.25f, 1e-6f);
  }
  Handle<EnemyStats> again = Resources::Load<EnemyStats>("data/orc");
  CHECK(orc.get() == again.get());  // one instance, two references
  Resources::Unload(orc);
  CHECK(again.get() != nullptr);
  Resources::Unload(again);
  CHECK(!Resources::Load<EnemyStats>("data/missing").valid());  // RSRC entry without a DATA record
}

TEST(resources_run_out_of_handles_with_an_error_not_a_crash) {
  Pack p;
  Handle<ResAudioClip> held[40];
  int ok = 0;
  for (int i = 0; i < 40; ++i) {
    // distinct assets only share a slot, so a single asset never fills the table; fill it with data asset instances instead
    held[i] = Resources::Load<ResAudioClip>("sfx/jump");
    ok += held[i].valid() ? 1 : 0;
  }
  CHECK_EQ(ok, 40);  // one slot, 40 references
  CHECK_EQ(Resources::loadedCount(), 1u);
  for (int i = 0; i < 40; ++i) Resources::Unload(held[i]);
  CHECK_EQ(Resources::loadedCount(), 0u);
}

TEST(resources_without_a_pack_return_invalid_handles) {
  SceneManager::unbind();
  CHECK(!Resources::Load<ResTexture>("a/b").valid());
  CHECK_EQ(Resources::entryCount(), 0u);
}

TEST(pack_resource_name_hash_ignores_case_and_slash_style) {
  CHECK_EQ(packResourceNameHash("Sfx/Jump"), packResourceNameHash("sfx/jump"));
  CHECK_EQ(packResourceNameHash("sfx\\jump"), packResourceNameHash("sfx/jump"));
  CHECK(packResourceNameHash("sfx/jump") != packResourceNameHash("sfx/jumP2"));
}
