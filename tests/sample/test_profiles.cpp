// F4.01 generator: make_sample.py --profile ps2|psp --out <dir>. The ps2 output must be byte-identical to the committed sample (so the
// committed files are never stale), and the psp output may differ from it ONLY in ProjectSettings (Project.json target and
// ConsoleTarget.json). The psp project is then cooked (for linux: the data is the same, the platform backends differ) and the bot plays
// the whole game on it, with the UI checked at 480x272.
#include <algorithm>
#include <fstream>
#include <sstream>

#include "sample_harness.h"

using namespace sample;

namespace {
std::string slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool generate(const char* profile, const fs::path& out) {
  const std::string cmd = std::string("python3 \"") + RENSGA_SAMPLE_DIR + "/tools/make_sample.py\" --profile " + profile + " --out \"" + out.string() +
                          "\" > /dev/null 2>&1";
  return system(cmd.c_str()) == 0;
}

// Every file of `a` (relative path) except Library/ and the hand-written non-project files; sorted.
std::vector<std::string> files(const fs::path& root) {
  std::vector<std::string> v;
  for (auto& e : fs::recursive_directory_iterator(root)) {
    if (!e.is_regular_file()) continue;
    const std::string rel = fs::relative(e.path(), root).string();
    if (rel.rfind("Library/", 0) == 0 || rel.rfind("tools/", 0) == 0 || rel == "README.md") continue;
    v.push_back(rel);
  }
  std::sort(v.begin(), v.end());
  return v;
}
}  // namespace

TEST(profiles_ps2_output_matches_the_committed_sample) {
  if (system("python3 --version > /dev/null 2>&1") != 0) { printf("    (python3 not found: skipped)\n"); return; }
  fs::path out = tempDir("gen_ps2") / "proj";
  CHECK(generate("ps2", out));
  const std::vector<std::string> a = files(RENSGA_SAMPLE_DIR), b = files(out);
  CHECK(a == b);
  int different = 0;
  for (const std::string& rel : b) {
    if (slurp(fs::path(RENSGA_SAMPLE_DIR) / rel) != slurp(out / rel)) {
      fprintf(stderr, "    differs from the committed file: %s (run tools/make_sample.py)\n", rel.c_str());
      ++different;
    }
  }
  CHECK_EQ(different, 0);
}

TEST(profiles_psp_output_differs_from_ps2_only_in_project_settings) {
  if (system("python3 --version > /dev/null 2>&1") != 0) { printf("    (python3 not found: skipped)\n"); return; }
  fs::path base = tempDir("gen_psp");
  fs::path ps2 = base / "ps2", psp = base / "psp";
  CHECK(generate("ps2", ps2));
  CHECK(generate("psp", psp));
  const std::vector<std::string> a = files(ps2), b = files(psp);
  CHECK(a == b);
  int different = 0;
  for (const std::string& rel : b) {
    if (slurp(ps2 / rel) == slurp(psp / rel)) continue;
    ++different;
    CHECK(rel == "ProjectSettings/Project.json" || rel == "ProjectSettings/ConsoleTarget.json");
  }
  CHECK_EQ(different, 2);
  const std::string target = slurp(psp / "ProjectSettings/ConsoleTarget.json");
  CHECK(target.find("\"profile\": \"psp\"") != std::string::npos);
  CHECK(target.find("480") != std::string::npos && target.find("272") != std::string::npos);
  CHECK(target.find("rgba5551") != std::string::npos);
  CHECK(target.find("\"bpp\": 16") != std::string::npos);
  CHECK(target.find("\"sampleRate\": 22050") != std::string::npos);
  CHECK(target.find("Rigidbody2D") != std::string::npos && target.find("TilemapCollider2D") != std::string::npos);
  CHECK(slurp(psp / "ProjectSettings/Project.json").find("\"target\": \"psp\"") != std::string::npos);
  CHECK(fs::exists(psp / "Assets/Scripts/GameState.h"));   // a complete project
  CHECK(fs::exists(psp / "Streaming/music.wav"));
}

TEST(profiles_psp_project_cooks_and_the_bot_plays_the_whole_game) {
  if (system("python3 --version > /dev/null 2>&1") != 0) { printf("    (python3 not found: skipped)\n"); return; }
  fs::path psp = tempDir("gen_psp_play") / "psp";
  CHECK(generate("psp", psp));
  fs::path pack = cookProjectDir(psp, "linux");
  CHECK(!pack.empty());
  if (pack.empty()) return;
  Game g;
  CHECK(g.boot(pack, tempDir("psp_saves"), kTitleBot));
  bool done = false;
  for (u32 f = 0; f < 30000 && !done; ++f) {
    g.frame();
    done = SceneManager::activeSceneIndex() == static_cast<i32>(kResults);
  }
  CHECK(done);
  CHECK_EQ(GameState::finalCoins, GameState::kTotalCoins);
  printf("    psp profile project: bot completed, %d ticks\n", GameState::finalTicks);
  // UI at the PSP resolution.
  Game ui;
  CHECK(ui.boot(pack, tempDir("psp_ui"), kRoom));
  ui.fakeUploads();
  ui.run(4);
  RecordingRenderer rr;
  rr.w = 480;
  rr.h = 272;
  renderGameplay2D(*World::active(), rr);
  int texts = 0;
  for (const RecordingRenderer::Rec& r : rr.rects)
    if (r.isText) {
      ++texts;
      CHECK(r.tx >= 0 && r.tx + r.tw <= 480 && r.ty >= 0 && r.ty + r.th <= 272);
    }
  CHECK(texts >= 5);
}
