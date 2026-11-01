// F6.11 (A3, API 9): PlayerPrefs through the save store: types, HasKey / DeleteKey / DeleteAll, persistence across a "restart"
// (a fresh process = a fresh load from the same store), damaged files, limits, the quit hook.
#include "mem_fs.h"
#include "rensga/PlayerPrefs.h"
#include "rensga/Rensga.h"
#include "rensga/core/application.h"
#include "rensga/core/save.h"
#include "rensga_test.h"

using namespace rensga;

namespace {
struct Store {
  memfs::MemFs fs;
  FileSystemSaveStore store;
  Store() : store(fs) {
    Save::use(nullptr);
    PlayerPrefs::DeleteAll();  // forget what an earlier test left in the table
    Save::use(&store);
    fs.files.clear();
  }
  ~Store() {
    PlayerPrefs::DeleteAll();
    Save::use(nullptr);
    PlayerPrefs::DeleteAll();
  }
  // Pretend the process restarted: the table is dropped and the next call loads from the store again.
  void restart() {
    Save::use(nullptr);
    PlayerPrefs::DeleteAll();
    Save::use(&store);
  }
};
}  // namespace

TEST(playerprefs_types_and_defaults) {
  Store s;
  CHECK(!PlayerPrefs::HasKey("volume"));
  CHECK_EQ(PlayerPrefs::GetInt("volume", 7), 7);
  PlayerPrefs::SetInt("volume", 3);
  PlayerPrefs::SetFloat("gamma", 1.25f);
  PlayerPrefs::SetString("player", "Ana");
  CHECK(PlayerPrefs::HasKey("volume") && PlayerPrefs::HasKey("gamma") && PlayerPrefs::HasKey("player"));
  CHECK_EQ(PlayerPrefs::GetInt("volume"), 3);
  CHECK_NEAR(PlayerPrefs::GetFloat("gamma"), 1.25f, 1e-6f);
  CHECK(strcmp(PlayerPrefs::GetString("player"), "Ana") == 0);
  CHECK(strcmp(PlayerPrefs::GetString("nobody", "anon"), "anon") == 0);
  PlayerPrefs::SetInt("volume", 9);  // overwrite keeps one entry
  CHECK_EQ(PlayerPrefs::GetInt("volume"), 9);
  CHECK_EQ(PlayerPrefs::GetInt("player", 5), 5);  // wrong type reads as the default
  PlayerPrefs::DeleteKey("volume");
  CHECK(!PlayerPrefs::HasKey("volume"));
  PlayerPrefs::DeleteAll();
  CHECK(!PlayerPrefs::HasKey("gamma") && !PlayerPrefs::HasKey("player"));
}

TEST(playerprefs_survive_a_restart_after_save) {
  Store s;
  PlayerPrefs::SetInt("level", 4);
  PlayerPrefs::SetFloat("music", 0.5f);
  PlayerPrefs::SetString("name", "Rensga");
  CHECK(PlayerPrefs::Save());
  CHECK(s.fs.files.count("save:/prefs") == 1);
  s.restart();
  CHECK_EQ(PlayerPrefs::GetInt("level", -1), 4);
  CHECK_NEAR(PlayerPrefs::GetFloat("music", -1), 0.5f, 1e-6f);
  CHECK(strcmp(PlayerPrefs::GetString("name"), "Rensga") == 0);
}

TEST(playerprefs_unsaved_changes_are_lost_but_quit_saves) {
  Store s;
  PlayerPrefs::SetInt("a", 1);
  s.restart();
  CHECK(!PlayerPrefs::HasKey("a"));
  PlayerPrefs::SetInt("a", 2);
  Application::reset();
  Application::Quit();  // runs the hooks: PlayerPrefs writes itself
  Application::reset();
  s.restart();
  CHECK_EQ(PlayerPrefs::GetInt("a", -1), 2);
}

TEST(playerprefs_damaged_file_reads_as_empty) {
  Store s;
  PlayerPrefs::SetInt("x", 1);
  CHECK(PlayerPrefs::Save());
  s.fs.corrupt("save:/prefs");
  s.restart();
  CHECK(!PlayerPrefs::HasKey("x"));
  PlayerPrefs::SetInt("x", 5);  // and it still works
  CHECK(PlayerPrefs::Save());
  s.restart();
  CHECK_EQ(PlayerPrefs::GetInt("x"), 5);
}

TEST(playerprefs_limits) {
  Store s;
  char key[40];
  for (u32 i = 0; i < PlayerPrefs::kMaxEntries; ++i) {
    snprintf(key, sizeof key, "k%u", i);
    PlayerPrefs::SetInt(key, static_cast<i32>(i));
  }
  PlayerPrefs::SetInt("one_too_many", 1);  // refused, logged
  CHECK(!PlayerPrefs::HasKey("one_too_many"));
  CHECK_EQ(PlayerPrefs::GetInt("k63"), 63);
  PlayerPrefs::DeleteKey("k0");
  PlayerPrefs::SetInt("fits_now", 1);
  CHECK(PlayerPrefs::HasKey("fits_now"));
  PlayerPrefs::SetInt("this_key_is_far_too_long_for_the_table_31", 1);
  CHECK(!PlayerPrefs::HasKey("this_key_is_far_too_long_for_the_table_31"));
  PlayerPrefs::DeleteKey("k1");
  char longText[100];
  memset(longText, 'z', sizeof longText);
  longText[99] = 0;
  PlayerPrefs::SetString("long", longText);
  CHECK_EQ(strlen(PlayerPrefs::GetString("long")), static_cast<size_t>(PlayerPrefs::kMaxStringLength));
  CHECK(PlayerPrefs::Save());  // 64 full entries still fit the store
}

TEST(playerprefs_without_a_store_keep_values_in_memory) {
  Save::use(nullptr);
  PlayerPrefs::DeleteAll();
  PlayerPrefs::SetInt("m", 1);
  CHECK_EQ(PlayerPrefs::GetInt("m"), 1);
  CHECK(!PlayerPrefs::Save());
  PlayerPrefs::DeleteAll();
}
