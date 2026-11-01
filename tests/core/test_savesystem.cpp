// F6.11 (A3, API 9): SaveSystem slots: versions, migration, metadata, delete, damaged files, limits, platform metadata hand-off.
#include "mem_fs.h"
#include "rensga/Rensga.h"
#include "rensga/SaveSystem.h"
#include "rensga/core/save.h"
#include "rensga/core/system.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
struct SaveV1 { i32 level; i32 coins; };
struct SaveV2 { i32 level; i32 coins; i32 lives; };

bool migrate1to2(u32 from, const void* in, u32 inSize, void* out, u32 cap, u32* outSize) {
  if (from != 1 || inSize != sizeof(SaveV1) || cap < sizeof(SaveV2)) return false;
  SaveV1 old;
  memcpy(&old, in, sizeof old);
  SaveV2 n = {old.level, old.coins, 3};  // v2 added lives, default 3
  memcpy(out, &n, sizeof n);
  *outSize = sizeof n;
  return true;
}

struct Store {
  memfs::MemFs fs;
  FileSystemSaveStore store;
  Store() : store(fs) { Save::use(&store); }
  ~Store() { Save::use(nullptr); }
};

class MetaPlatform : public NullPlatform {
 public:
  char title[32] = {}, detail[32] = {};
  int calls = 0;
  void setSaveMetadata(const char* t, const char* d) override {
    ++calls;
    strncpy(title, t, 31);
    strncpy(detail, d, 31);
  }
};
}  // namespace

TEST(savesystem_write_read_roundtrip) {
  Store s;
  SaveV2 a = {5, 120, 2};
  CHECK(!SaveSystem::Exists(0));
  CHECK(SaveSystem::Write(0, 2, &a, sizeof a));
  CHECK(SaveSystem::Exists(0) && !SaveSystem::Exists(1));
  SaveV2 b = {};
  CHECK_EQ(SaveSystem::Read(0, 2, &b, sizeof b), static_cast<i32>(sizeof b));
  CHECK(b.level == 5 && b.coins == 120 && b.lives == 2);
  CHECK(s.fs.files.count("save:/slot0") == 1);
  CHECK_EQ(SaveSystem::Read(1, 2, &b, sizeof b), -1);  // empty slot
  CHECK_EQ(SaveSystem::Read(0, 2, &b, 4), -1);          // buffer too small
}

TEST(savesystem_older_versions_go_through_the_migration) {
  Store s;
  SaveV1 old = {7, 33};
  CHECK(SaveSystem::Write(2, 1, &old, sizeof old));
  SaveV2 n = {};
  CHECK_EQ(SaveSystem::Read(2, 2, &n, sizeof n), -1);  // no migration given
  CHECK_EQ(SaveSystem::Read(2, 2, &n, sizeof n, &migrate1to2), static_cast<i32>(sizeof n));
  CHECK(n.level == 7 && n.coins == 33 && n.lives == 3);
  SaveSystem::SlotInfo info;
  CHECK(SaveSystem::GetInfo(2, &info) && info.version == 1 && info.size == sizeof old);
}

TEST(savesystem_refuses_a_newer_save) {
  Store s;
  SaveV2 a = {1, 2, 3};
  CHECK(SaveSystem::Write(0, 3, &a, sizeof a));
  SaveV2 b = {};
  CHECK_EQ(SaveSystem::Read(0, 2, &b, sizeof b, &migrate1to2), -1);
}

TEST(savesystem_metadata_and_slot_info) {
  Store s;
  SaveSystem::Metadata m;
  strcpy(m.title, "Forest");
  strcpy(m.detail, "Level 2  00:12:30");
  m.icon.bytes[3] = 9;
  SaveV1 a = {2, 10};
  CHECK(SaveSystem::Write(3, 1, &a, sizeof a, &m));
  SaveSystem::SlotInfo info;
  CHECK(SaveSystem::GetInfo(3, &info));
  CHECK(info.exists && info.version == 1 && info.size == sizeof a);
  CHECK(strcmp(info.meta.title, "Forest") == 0 && strcmp(info.meta.detail, "Level 2  00:12:30") == 0);
  CHECK(info.meta.icon == m.icon);
  CHECK(!SaveSystem::GetInfo(4, &info) && !info.exists);
}

TEST(savesystem_hands_title_and_detail_to_the_platform) {
  Store s;
  MetaPlatform plat;
  system_services::bind(&plat);
  SaveSystem::Metadata m;
  strcpy(m.title, "Cave");
  strcpy(m.detail, "3 coins");
  SaveV1 a = {1, 1};
  CHECK(SaveSystem::Write(0, 1, &a, sizeof a, &m));
  system_services::bind(nullptr);
  CHECK_EQ(plat.calls, 1);
  CHECK(strcmp(plat.title, "Cave") == 0 && strcmp(plat.detail, "3 coins") == 0);
}

TEST(savesystem_delete_and_overwrite) {
  Store s;
  SaveV1 a = {1, 1}, b = {9, 9};
  CHECK(SaveSystem::Write(0, 1, &a, sizeof a));
  CHECK(SaveSystem::Write(0, 1, &b, sizeof b));
  SaveV1 r = {};
  CHECK(SaveSystem::Read(0, 1, &r, sizeof r) == static_cast<i32>(sizeof r) && r.level == 9);
  CHECK(SaveSystem::Delete(0));
  CHECK(!SaveSystem::Exists(0));
  CHECK_EQ(SaveSystem::Read(0, 1, &r, sizeof r), -1);
  SaveSystem::SlotInfo info;
  CHECK(!SaveSystem::GetInfo(0, &info));
  CHECK(SaveSystem::Delete(5));  // deleting an empty slot is fine
  CHECK(SaveSystem::Write(0, 1, &a, sizeof a));  // and the slot can be used again
  CHECK(SaveSystem::Exists(0));
}

TEST(savesystem_damaged_slot_reads_as_missing) {
  Store s;
  SaveV1 a = {4, 4};
  CHECK(SaveSystem::Write(1, 1, &a, sizeof a));
  s.fs.corrupt("save:/slot1");
  SaveV1 r = {};
  CHECK_EQ(SaveSystem::Read(1, 1, &r, sizeof r), -1);
  CHECK(!SaveSystem::Exists(1));
}

TEST(savesystem_limits) {
  Store s;
  static u8 big[SaveSystem::kMaxPayload + 4] = {};
  CHECK(SaveSystem::Write(0, 1, big, SaveSystem::kMaxPayload));
  CHECK_EQ(SaveSystem::Read(0, 1, big, SaveSystem::kMaxPayload), static_cast<i32>(SaveSystem::kMaxPayload));
  CHECK(!SaveSystem::Write(0, 1, big, SaveSystem::kMaxPayload + 1));
  CHECK(!SaveSystem::Write(SaveSystem::kMaxSlots, 1, big, 4));  // slot out of range
  CHECK(!SaveSystem::Write(0, 0, big, 4));                      // version 0 is the "deleted" marker
  s.fs.failWrites = true;
  CHECK(!SaveSystem::Write(1, 1, big, 4));  // a full card reports failure
  s.fs.failWrites = false;
  SaveV1 a = {1, 2};
  CHECK(SaveSystem::Write(7, 1, &a, sizeof a));  // the last slot works
  Save::use(nullptr);
  CHECK(!SaveSystem::Write(0, 1, &a, sizeof a));  // no store
  CHECK(!SaveSystem::Exists(0));
}
