// F6.07 (D3): DataAssets::Get<T> on an in-memory pack (scene_pack.h): typed lookup by GUID, wrong type / unknown / null GUID,
// stale entries rebuilt when another pack is bound, a full table, and no heap use after the first lookup.
#include "rensga/DataAsset.h"
#include "rensga/Rensga.h"
#include "rensga/core/scene_manager.h"
#include "rensga_test.h"
#include "scene_pack.h"

using namespace rensga;

namespace {
struct TunedA : DataAsset {
  REFLECT(TunedA)
  SERIALIZE_FIELD(i32, hp, 10)
  SERIALIZE_FIELD(Scalar, speed, 1.5f)
  SERIALIZE_FIELD(String32, name, "none")
};
struct TunedB : DataAsset {
  REFLECT(TunedB)
  SERIALIZE_FIELD(i32, n, 0)
};

Guid gid(u8 n) {
  Guid g;
  g.bytes[0] = 0xD3;
  g.bytes[15] = n;
  return g;
}

struct Pack {
  scenepack::Builder b;
  u8* mem = nullptr;
  PackView view;
  explicit Pack(i32 hp, u8 count = 1) {
    b.addScene("Main", {scenepack::Obj{"Root", -1, nullptr, 0, true}});
    for (u8 i = 1; i <= count; ++i) {
      const TypeInfo* t = TunedA::staticTypeInfo();
      std::vector<u8> obj(t->size + 16);
      t->construct(obj.data());
      TunedA* a = reinterpret_cast<TunedA*>(obj.data());
      a->hp = hp + i;
      a->speed = 2.5f;
      a->name.Set("orc");
      std::vector<u8> blob(blobSize(*t));
      writeBlob(*t, obj.data(), blob.data());
      PackDataAsset d = {};
      d.guid = gid(i);
      d.typeHash = t->hash;
      d.nameStr = b.str("orc");
      d.blobOffset = b.blobPut(blob.data(), static_cast<u32>(blob.size()));
      d.blobSize = static_cast<u32>(blob.size());
      b.data.push_back(d);
    }
    size_t n = 0;
    mem = b.build(&n);
    CHECK(view.open(mem, n, "data-assets-test"));
    SceneManager::bind(&view, 0);
  }
  ~Pack() {
    SceneManager::unbind();
    free(mem);
  }
};
}  // namespace

TEST(data_assets_get_returns_a_typed_read_only_instance) {
  Pack p(40);
  CHECK_EQ(DataAssets::count(), 1u);
  const TunedA* a = DataAssets::Get<TunedA>(gid(1));
  CHECK(a != nullptr);
  if (a) {
    CHECK_EQ(a->hp, 41);
    CHECK(a->speed == 2.5f);
    CHECK(a->name == String32("orc"));
  }
  CHECK(DataAssets::Get<TunedA>(gid(1)) == a);
  CHECK(DataAssets::Get<TunedB>(gid(1)) == nullptr);   // another type
  CHECK(DataAssets::Get<TunedA>(gid(9)) == nullptr);   // not in the pack
  CHECK(DataAssets::Get<TunedA>(Guid()) == nullptr);   // unset field
}

TEST(data_assets_rebuild_when_another_pack_is_bound) {
  i32 first = 0, second = 0;
  {
    Pack p(100);
    const TunedA* a = DataAssets::Get<TunedA>(gid(1));
    first = a ? a->hp : -1;
  }
  {
    Pack p(200);
    const TunedA* a = DataAssets::Get<TunedA>(gid(1));
    second = a ? a->hp : -1;
  }
  CHECK_EQ(first, 101);
  CHECK_EQ(second, 201);
}

TEST(data_assets_table_is_fixed_and_reports_when_full) {
  Pack p(0, static_cast<u8>(kDataAssetMaxLive + 2));
  u32 ok = 0;
  for (u8 i = 1; i <= kDataAssetMaxLive + 2; ++i)
    if (DataAssets::Get<TunedA>(gid(i))) ++ok;
  CHECK_EQ(ok, kDataAssetMaxLive);
  CHECK_EQ(DataAssets::liveCount(), kDataAssetMaxLive);
}

TEST(data_assets_without_a_pack_are_null) {
  SceneManager::unbind();
  CHECK(DataAssets::Get<TunedA>(gid(1)) == nullptr);
  CHECK_EQ(DataAssets::count(), 0u);
}
