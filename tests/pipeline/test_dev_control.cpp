// F5.20: the game side of Pause / Step. devControlPoll() reads RENSGA_DEVCTL (host0:/rensga.ctl on a PSP).
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "rensga/core/app_events.h"
#include "rensga/core/application.h"
#include "rensga/core/dev_control.h"
#include "rensga_test.h"

using namespace rensga;
namespace fs = std::filesystem;

namespace {
fs::path g_ctl;
void write(const char* text) {
  std::ofstream(g_ctl, std::ios::trunc) << text;
}
// Polls until the answer differs from `not_`, at most 40 iterations (a running game looks at the file every 8th one).
u32 pollUntilNot(u32 not_) {
  for (int i = 0; i < 40; ++i) {
    const u32 v = devControlPoll();
    if (v != not_) return v;
  }
  return not_;
}
}  // namespace

TEST(devctl_pause_resume_step) {
  g_ctl = fs::path(RENSGA_TEST_TMP_DIR) / "devctl" / "rensga.ctl";
  fs::create_directories(g_ctl.parent_path());
  fs::remove(g_ctl);
  setenv("RENSGA_DEVCTL", g_ctl.c_str(), 1);
  for (int i = 0; i < 20; ++i) CHECK_EQ(devControlPoll(), 1u);  // no file: run normally

  write("1 pause\n");
  CHECK_EQ(pollUntilNot(1), 0u);
  CHECK_EQ(devControlPoll(), 0u);  // stays paused

  write("1 pause\n2 step 5\n");
  CHECK_EQ(devControlPoll(), 5u);  // exactly the requested frames, once
  CHECK_EQ(devControlPoll(), 0u);  // and paused again
  CHECK_EQ(devControlPoll(), 0u);

  write("1 pause\n2 step 5\n3 step 1\n");
  CHECK_EQ(devControlPoll(), 1u);  // step 1 is a one-frame step, not a resume ...
  CHECK_EQ(devControlPoll(), 0u);  // ... the game is paused right after it

  write("1 pause\n2 step 5\n3 step 1\n4 resume\n");
  CHECK_EQ(devControlPoll(), 1u);
  for (int i = 0; i < 20; ++i) CHECK_EQ(devControlPoll(), 1u);

  write("1 pause\n2 step 5\n3 step 1\n4 resume\n5 pause\n6 step 3\n");  // a running game hears pause and step in one read
  CHECK_EQ(pollUntilNot(1), 3u);
  CHECK_EQ(devControlPoll(), 0u);
}

TEST(devctl_stale_and_bad_lines_are_ignored) {
  write("1 pause\n2 step 5\n3 step 1\n4 resume\n5 pause\n6 step 3\n7 resume\n");
  CHECK_EQ(pollUntilNot(0), 1u);
  write("garbage\n\n7 resume\n3 pause\nx y\n");  // stale seq and junk change nothing
  for (int i = 0; i < 30; ++i) CHECK_EQ(devControlPoll(), 1u);
}

TEST(devctl_apppause_and_quit) {
  write("8 apppause 1\n9 apppause 0\n");
  for (int i = 0; i < 20; ++i) devControlPoll();
  CHECK_EQ(applicationResumeCount(), 0u);  // queued, delivered by pumpApplicationEvents
  CHECK(!Application::quitRequested());
  write("8 apppause 1\n9 apppause 0\n10 quit\n");
  for (int i = 0; i < 20; ++i) devControlPoll();
  CHECK(Application::quitRequested());
}
