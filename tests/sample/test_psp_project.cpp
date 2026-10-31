// F4.07: samples/platformer2d_psp is GENERATED (make_sample.py --profile psp). This test regenerates both profiles into a temp folder and
// compares them with the committed folders byte for byte: the committed PSP project must equal the psp output, the committed PS2
// sample must equal the ps2 output, and the two outputs may differ only in ProjectSettings/{Project,ConsoleTarget}.json.
// Hand-written files (README.md, tools/, Library/) are not part of the comparison.
#include <algorithm>
#include <fstream>
#include <sstream>

#include "sample_harness.h"

using namespace sample;

namespace {
std::string slurpFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool generateProfile(const char* profile, const fs::path& out) {
  const std::string cmd = std::string("python3 \"") + RENSGA_SAMPLE_DIR + "/tools/make_sample.py\" --profile " + profile + " --out \"" +
                          out.string() + "\" > /dev/null 2>&1";
  return system(cmd.c_str()) == 0;
}

std::vector<std::string> projectFiles(const fs::path& root) {
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

// Compares `committed` with `generated`; prints each difference. Returns the number of differing or missing files.
int diffTrees(const fs::path& committed, const fs::path& generated) {
  const std::vector<std::string> a = projectFiles(committed), b = projectFiles(generated);
  int bad = 0;
  for (const std::string& rel : b)
    if (!std::binary_search(a.begin(), a.end(), rel)) {
      fprintf(stderr, "    missing from %s: %s\n", committed.string().c_str(), rel.c_str());
      ++bad;
    }
  for (const std::string& rel : a)
    if (!std::binary_search(b.begin(), b.end(), rel)) {
      fprintf(stderr, "    not generated (stale file in %s): %s\n", committed.string().c_str(), rel.c_str());
      ++bad;
    }
  for (const std::string& rel : b) {
    if (!std::binary_search(a.begin(), a.end(), rel)) continue;
    if (slurpFile(committed / rel) != slurpFile(generated / rel)) {
      fprintf(stderr, "    differs from the generator output: %s/%s (run tools/make_sample.py)\n", committed.string().c_str(), rel.c_str());
      ++bad;
    }
  }
  return bad;
}
}  // namespace

TEST(psp_project_is_generated_and_differs_from_ps2_only_in_project_settings) {
  if (system("python3 --version > /dev/null 2>&1") != 0) { printf("    (python3 not found: skipped)\n"); return; }
  fs::path base = tempDir("gen_psp_project");
  fs::path ps2 = base / "ps2", psp = base / "psp";
  CHECK(generateProfile("ps2", ps2));
  CHECK(generateProfile("psp", psp));
  // 1. the committed folders equal the generator output, byte for byte
  CHECK(fs::exists(RENSGA_SAMPLE_PSP_DIR));
  CHECK_EQ(diffTrees(RENSGA_SAMPLE_DIR, ps2), 0);
  CHECK_EQ(diffTrees(RENSGA_SAMPLE_PSP_DIR, psp), 0);
  // 2. the committed projects differ only in ProjectSettings (Assets/ and Streaming/ byte for byte)
  const std::vector<std::string> a = projectFiles(RENSGA_SAMPLE_DIR), b = projectFiles(RENSGA_SAMPLE_PSP_DIR);
  CHECK(a == b);
  int different = 0, assets = 0;
  for (const std::string& rel : b) {
    const bool settings = rel.rfind("ProjectSettings/", 0) == 0;
    if (!settings) ++assets;
    if (slurpFile(fs::path(RENSGA_SAMPLE_DIR) / rel) == slurpFile(fs::path(RENSGA_SAMPLE_PSP_DIR) / rel)) continue;
    ++different;
    CHECK(rel == "ProjectSettings/Project.json" || rel == "ProjectSettings/ConsoleTarget.json");
  }
  CHECK_EQ(different, 2);
  CHECK(assets > 100);
  // 3. the PSP expectations
  const std::string target = slurpFile(fs::path(RENSGA_SAMPLE_PSP_DIR) / "ProjectSettings/ConsoleTarget.json");
  CHECK(target.find("\"profile\": \"psp\"") != std::string::npos);
  CHECK(target.find("480") != std::string::npos && target.find("272") != std::string::npos);
  CHECK(target.find("rgba5551") != std::string::npos);
  CHECK(target.find("\"bpp\": 16") != std::string::npos);
  CHECK(target.find("\"sampleRate\": 22050") != std::string::npos);
  CHECK(slurpFile(fs::path(RENSGA_SAMPLE_PSP_DIR) / "ProjectSettings/Project.json").find("\"target\": \"psp\"") != std::string::npos);
  CHECK(fs::exists(fs::path(RENSGA_SAMPLE_PSP_DIR) / "README.md"));
  CHECK(fs::exists(fs::path(RENSGA_SAMPLE_PSP_DIR) / "Streaming/music3.wav"));
}
